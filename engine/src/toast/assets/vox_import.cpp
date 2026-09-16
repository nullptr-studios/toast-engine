#include "vox_import.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tracy/Tracy.hpp>
#include <unordered_map>

namespace assets {

using toast::voxel::BrickPool;
using toast::voxel::k_brick_dim;
using toast::voxel::k_palette_size;
using toast::voxel::LatticeOrientation;
using toast::voxel::LatticePlacement;
using toast::voxel::PaletteEntry;
using toast::voxel::Volume;

auto voxOrientationFromByte(uint8_t rotation) -> std::optional<LatticeOrientation> {
	const uint8_t row_x = rotation & 0x03u;
	const uint8_t row_y = (rotation >> 2) & 0x03u;
	if (row_x > 2 || row_y > 2 || row_x == row_y) {
		return std::nullopt;
	}

	LatticeOrientation out;
	out.source = {row_x, row_y, static_cast<uint8_t>(3u - row_x - row_y)};
	out.flip = {((rotation >> 4) & 1u) != 0, ((rotation >> 5) & 1u) != 0, ((rotation >> 6) & 1u) != 0};
	return out;
}

auto voxOrientationToByte(const LatticeOrientation& orientation) -> uint8_t {
	auto out = static_cast<uint8_t>(orientation.source[0] | (orientation.source[1] << 2));
	for (uint32_t axis = 0; axis < 3; ++axis) {
		if (orientation.flip[axis]) {
			out |= static_cast<uint8_t>(1u << (4 + axis));
		}
	}
	return out;
}

auto composeVoxTransforms(const VoxTransform& parent, const VoxTransform& local) -> VoxTransform {
	VoxTransform out;
	for (uint32_t axis = 0; axis < 3; ++axis) {
		const uint8_t through = parent.orientation.source[axis];
		out.orientation.source[axis] = local.orientation.source[through];
		out.orientation.flip[axis] = parent.orientation.flip[axis] != local.orientation.flip[through];

		// The parent rotation carries the child translation
		const int32_t along = local.translation[through];
		out.translation[axis] = parent.translation[axis] + (parent.orientation.flip[axis] ? -along : along);
	}
	return out;
}

auto voxPlacementOf(const VoxTransform& world, glm::uvec3 model_dims) -> LatticePlacement {
	LatticePlacement out;
	out.orientation = world.orientation;
	for (uint32_t axis = 0; axis < 3; ++axis) {
		const auto size = static_cast<int32_t>(model_dims[world.orientation.source[axis]]);
		out.offset[axis] =
		    world.orientation.flip[axis] ? world.translation[axis] + (size + 1) / 2 : world.translation[axis] - size / 2;
	}
	return out;
}

auto flattenVoxScene(const VoxScene& scene) -> std::vector<VoxPlacement> {
	std::vector<VoxPlacement> out;

	if (scene.nodes.empty()) {
		out.reserve(scene.models.size());
		for (uint32_t i = 0; i < scene.models.size(); ++i) {
			out.push_back(VoxPlacement {.model = i, .placement = voxPlacementOf(VoxTransform {}, scene.models[i].dims)});
		}
		return out;
	}

	struct Pending {
		uint32_t node = 0;
		VoxTransform world;
		std::string name;
		bool hidden = false;
	};

	// Children pushed in reverse so they pop in file order
	std::vector<Pending> stack;
	stack.push_back(Pending {});
	while (!stack.empty()) {
		const Pending current = stack.back();
		stack.pop_back();

		const VoxNode& node = scene.nodes[current.node];
		const VoxTransform world = composeVoxTransforms(current.world, node.local);
		const std::string name = node.name.empty() ? current.name : node.name;
		const bool hidden = current.hidden || node.hidden;

		if (node.model.has_value()) {
			out.push_back(
			    VoxPlacement {
			      .model = *node.model,
			      .name = name,
			      .hidden = hidden,
			      .placement = voxPlacementOf(world, scene.models[*node.model].dims),
			    }
			);
		}

		for (size_t i = node.children.size(); i-- > 0;) {
			stack.push_back(Pending {.node = node.children[i], .world = world, .name = name, .hidden = hidden});
		}
	}
	return out;
}

auto buildVoxVolume(const VoxModel& model, BrickPool& pool) -> std::optional<Volume> {
	ZoneScoped;

	const glm::uvec3 bricks = (model.dims + (k_brick_dim - 1u)) / k_brick_dim;
	Volume volume(pool, glm::max(bricks, glm::uvec3(1)));
	for (const VoxVoxel& voxel : model.voxels) {
		const glm::ivec3 at(voxel.x, voxel.y, voxel.z);
		volume.setVoxel(at, voxel.palette_index);
		if (volume.materialAt(at) != voxel.palette_index) {
			return std::nullopt;
		}
	}
	return std::optional<Volume>(std::move(volume));
}

namespace {

class VoxReader {
public:
	explicit VoxReader(std::span<const uint8_t> data) : m_data(data) { }

	[[nodiscard]]
	auto remaining() const -> size_t {
		return m_data.size() - m_offset;
	}

	[[nodiscard]]
	auto offset() const -> size_t {
		return m_offset;
	}

	void seek(size_t offset) { m_offset = offset; }

	void skip(size_t bytes) {
		require(bytes);
		m_offset += bytes;
	}

	[[nodiscard]]
	auto u8() -> uint8_t {
		require(1);
		return m_data[m_offset++];
	}

	[[nodiscard]]
	auto i32() -> int32_t {
		require(4);
		int32_t value = 0;
		std::memcpy(&value, m_data.data() + m_offset, 4);
		m_offset += 4;
		return value;
	}

	[[nodiscard]]
	auto tag() -> std::array<char, 4> {
		require(4);
		std::array<char, 4> out {};
		std::memcpy(out.data(), m_data.data() + m_offset, 4);
		m_offset += 4;
		return out;
	}

	/// @brief MagicaVoxel writes it unterminated
	[[nodiscard]]
	auto string() -> std::string {
		const int32_t length = i32();
		if (length < 0) {
			throw std::runtime_error(".vox: a string of negative length");
		}
		require(static_cast<size_t>(length));
		std::string out(reinterpret_cast<const char*>(m_data.data() + m_offset), static_cast<size_t>(length));
		m_offset += static_cast<size_t>(length);
		return out;
	}

	[[nodiscard]]
	auto dict() -> std::unordered_map<std::string, std::string> {
		const int32_t count = i32();
		if (count < 0) {
			throw std::runtime_error(".vox: a dictionary of negative size");
		}

		std::unordered_map<std::string, std::string> out;
		for (int32_t i = 0; i < count; ++i) {
			std::string key = string();
			out.insert_or_assign(std::move(key), string());
		}
		return out;
	}

private:
	void require(size_t bytes) const {
		if (remaining() < bytes) {
			throw std::runtime_error(".vox: truncated - a chunk claims more data than the file holds");
		}
	}

	std::span<const uint8_t> m_data;
	size_t m_offset = 0;
};

using VoxDict = std::unordered_map<std::string, std::string>;

[[nodiscard]]
auto voxDictInt(const VoxDict& dict, const std::string& key, int32_t fallback) -> int32_t {
	const auto found = dict.find(key);
	if (found == dict.end()) {
		return fallback;
	}
	try {
		return std::stoi(found->second);
	} catch (const std::exception&) { return fallback; }
}

[[nodiscard]]
auto voxDictFloat(const VoxDict& dict, const std::string& key, float fallback) -> float {
	const auto found = dict.find(key);
	if (found == dict.end()) {
		return fallback;
	}
	try {
		return std::stof(found->second);
	} catch (const std::exception&) { return fallback; }
}

[[nodiscard]]
auto voxDictTranslation(const VoxDict& dict) -> glm::ivec3 {
	const auto found = dict.find("_t");
	if (found == dict.end()) {
		return glm::ivec3(0);
	}

	glm::ivec3 out(0);
	size_t cursor = 0;
	for (int axis = 0; axis < 3; ++axis) {
		while (cursor < found->second.size() && found->second[cursor] == ' ') {
			++cursor;
		}
		if (cursor >= found->second.size()) {
			return out;
		}

		size_t consumed = 0;
		try {
			out[axis] = std::stoi(found->second.substr(cursor), &consumed);
		} catch (const std::exception&) { return out; }
		cursor += consumed;
	}
	return out;
}

[[nodiscard]]
auto voxByteFromUnit(float value) -> uint8_t {
	return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

struct VoxMaterial {
	float roughness = 0.0f;
	float metallic = 0.0f;
	float emission = 0.0f;
	bool transparent = false;
};

}

auto importVox(std::span<const uint8_t> data) -> VoxScene {
	ZoneScoped;

	VoxReader reader(data);
	if (reader.remaining() < 8) {
		throw std::runtime_error(".vox: too short to be a .vox file");
	}

	const std::array<char, 4> magic = reader.tag();
	if (std::memcmp(magic.data(), "VOX ", 4) != 0) {
		throw std::runtime_error(".vox: not a .vox file");
	}
	const int32_t version = reader.i32();
	if (version != 150 && version != 200) {
		throw std::runtime_error(".vox: version " + std::to_string(version) + " is not supported (this build reads 150 and 200)");
	}

	const std::array<char, 4> main = reader.tag();
	if (std::memcmp(main.data(), "MAIN", 4) != 0) {
		throw std::runtime_error(".vox: the first chunk must be MAIN");
	}
	const int32_t main_content = reader.i32();
	const int32_t main_children = reader.i32();
	if (main_content < 0 || main_children < 0) {
		throw std::runtime_error(".vox: MAIN declares a negative size");
	}
	reader.skip(static_cast<size_t>(main_content));

	VoxScene scene;
	bool has_rgba = false;
	std::unordered_map<uint32_t, VoxMaterial> materials;

	struct RawNode {
		VoxNode node;
		std::vector<int32_t> child_ids;
		bool is_child = false;
	};

	std::unordered_map<int32_t, RawNode> raw_nodes;
	std::vector<int32_t> node_order;

	const auto rawNodeFor = [&raw_nodes, &node_order](int32_t id) -> RawNode& {
		if (!raw_nodes.contains(id)) {
			node_order.push_back(id);
		}
		return raw_nodes[id];
	};

	const size_t end = reader.offset() + static_cast<size_t>(main_children);
	if (end > data.size()) {
		throw std::runtime_error(".vox: MAIN claims more children than the file holds");
	}

	while (reader.offset() < end) {
		const std::array<char, 4> id = reader.tag();
		const int32_t content = reader.i32();
		const int32_t children = reader.i32();
		if (content < 0 || children < 0) {
			throw std::runtime_error(".vox: a chunk declares a negative size");
		}
		const size_t after = reader.offset() + static_cast<size_t>(content) + static_cast<size_t>(children);
		if (after > end) {
			throw std::runtime_error(".vox: a chunk runs past the end of MAIN");
		}

		const auto is = [&id](const char* name) { return std::memcmp(id.data(), name, 4) == 0; };

		if (is("SIZE")) {
			const int32_t x = reader.i32();
			const int32_t y = reader.i32();
			const int32_t z = reader.i32();
			if (x <= 0 || y <= 0 || z <= 0 || x > static_cast<int32_t>(k_vox_max_dim) || y > static_cast<int32_t>(k_vox_max_dim) ||
			    z > static_cast<int32_t>(k_vox_max_dim)) {
				throw std::runtime_error(
				    ".vox: a model is " + std::to_string(x) + "x" + std::to_string(y) + "x" + std::to_string(z) +
				    ", outside MagicaVoxel's own 1 to 256 per axis"
				);
			}
			scene.models.push_back(VoxModel {.dims = glm::uvec3(x, y, z)});
		} else if (is("XYZI")) {
			if (scene.models.empty()) {
				throw std::runtime_error(".vox: an XYZI chunk with no SIZE before it");
			}
			VoxModel& model = scene.models.back();
			if (!model.voxels.empty()) {
				throw std::runtime_error(".vox: two XYZI chunks for one SIZE");
			}

			const int32_t count = reader.i32();
			if (count < 0) {
				throw std::runtime_error(".vox: a negative voxel count");
			}
			model.voxels.reserve(static_cast<size_t>(count));
			for (int32_t i = 0; i < count; ++i) {
				VoxVoxel voxel;
				voxel.x = reader.u8();
				voxel.y = reader.u8();
				voxel.z = reader.u8();
				voxel.palette_index = reader.u8();
				if (voxel.x >= model.dims.x || voxel.y >= model.dims.y || voxel.z >= model.dims.z) {
					throw std::runtime_error(".vox: a voxel lies outside the model its SIZE declared");
				}
				if (voxel.palette_index == toast::voxel::k_empty_palette_index) {
					continue;
				}
				model.voxels.push_back(voxel);
			}
		} else if (is("RGBA")) {
			// Entry i is palette index i + 1
			for (uint32_t i = 0; i < k_palette_size; ++i) {
				const uint8_t r = reader.u8();
				const uint8_t g = reader.u8();
				const uint8_t b = reader.u8();
				const uint8_t a = reader.u8();
				if (i + 1 >= k_palette_size) {
					continue;
				}

				PaletteEntry& entry = scene.palette.entries[i + 1];
				entry.albedo_r = r;
				entry.albedo_g = g;
				entry.albedo_b = b;
				if (a < 255) {
					entry.flags |= toast::voxel::k_entry_transparent;
				}
			}
			has_rgba = true;
		} else if (is("MATL")) {
			const int32_t index = reader.i32();
			const VoxDict attributes = reader.dict();
			if (index < 1 || index >= static_cast<int32_t>(k_palette_size)) {
				scene.warnings.push_back("a MATL chunk names palette index " + std::to_string(index) + ", which cannot exist");
			} else {
				const auto type = attributes.find("_type");
				VoxMaterial material;
				material.roughness = voxDictFloat(attributes, "_rough", 0.0f);
				material.metallic = voxDictFloat(attributes, "_metal", 0.0f);
				// _flux multiplies the emission by 2^flux
				material.emission = voxDictFloat(attributes, "_emit", 0.0f) * std::pow(2.0f, voxDictFloat(attributes, "_flux", 0.0f));
				material.transparent =
				    (type != attributes.end() && type->second == "_glass") || voxDictFloat(attributes, "_alpha", 1.0f) < 1.0f;
				materials.insert_or_assign(static_cast<uint32_t>(index), material);
			}
		} else if (is("nTRN")) {
			const int32_t node_id = reader.i32();
			const VoxDict attributes = reader.dict();
			const int32_t child_id = reader.i32();
			(void)reader.i32();    // reserved -1
			(void)reader.i32();    // layer
			const int32_t frames = reader.i32();
			if (frames < 1) {
				throw std::runtime_error(".vox: an nTRN chunk with no frames");
			}

			RawNode& raw = rawNodeFor(node_id);
			const auto name = attributes.find("_name");
			if (name != attributes.end()) {
				raw.node.name = name->second;
			}
			raw.node.hidden = voxDictInt(attributes, "_hidden", 0) != 0;

			for (int32_t i = 0; i < frames; ++i) {
				const VoxDict frame = reader.dict();
				if (i != 0) {
					continue;
				}

				raw.node.local.translation = voxDictTranslation(frame);
				const auto rotation = frame.find("_r");
				if (rotation != frame.end()) {
					const std::optional<LatticeOrientation> decoded =
					    voxOrientationFromByte(static_cast<uint8_t>(voxDictInt(frame, "_r", 0)));
					if (decoded.has_value()) {
						raw.node.local.orientation = *decoded;
					} else {
						scene.warnings.push_back(
						    "node " + std::to_string(node_id) + " carries rotation byte " + rotation->second +
						    ", which is not one of the 48 lattice orientations; placed unrotated"
						);
					}
				}
			}
			if (frames > 1) {
				scene.warnings.push_back(
				    "node " + std::to_string(node_id) + " has " + std::to_string(frames) + " transform frames; only the first is imported"
				);
			}

			raw.child_ids.push_back(child_id);
			rawNodeFor(child_id).is_child = true;
		} else if (is("nGRP")) {
			const int32_t node_id = reader.i32();
			(void)reader.dict();
			const int32_t count = reader.i32();
			if (count < 0) {
				throw std::runtime_error(".vox: an nGRP chunk with a negative child count");
			}

			RawNode& raw = rawNodeFor(node_id);
			for (int32_t i = 0; i < count; ++i) {
				const int32_t child_id = reader.i32();
				raw.child_ids.push_back(child_id);
				rawNodeFor(child_id).is_child = true;
			}
		} else if (is("nSHP")) {
			const int32_t node_id = reader.i32();
			(void)reader.dict();
			const int32_t count = reader.i32();
			if (count < 1) {
				throw std::runtime_error(".vox: an nSHP chunk with no models");
			}

			RawNode& raw = rawNodeFor(node_id);
			for (int32_t i = 0; i < count; ++i) {
				const int32_t model_id = reader.i32();
				(void)reader.dict();
				if (model_id < 0 || model_id >= static_cast<int32_t>(scene.models.size())) {
					throw std::runtime_error(
					    ".vox: a shape references model " + std::to_string(model_id) + ", which the file does not hold"
					);
				}
				if (i == 0) {
					raw.node.model = static_cast<uint32_t>(model_id);
				}
			}
			if (count > 1) {
				scene.warnings.push_back(
				    "node " + std::to_string(node_id) + " holds " + std::to_string(count) + " model frames; only the first is imported"
				);
			}
		} else if (is("PACK") || is("LAYR") || is("rOBJ") || is("rCAM") || is("NOTE") || is("IMAP")) {
		} else {
			scene.warnings.push_back(
			    "unknown chunk '" + std::string(id.data(), 4) + "' skipped, which is how a newer MagicaVoxel adds things"
			);
		}

		reader.seek(after);
	}

	for (const VoxModel& model : scene.models) {
		if (model.voxels.empty()) {
			scene.warnings.push_back("a model holds no voxels");
		}
	}

	if (!has_rgba) {
		for (uint32_t i = 1; i < k_palette_size; ++i) {
			scene.palette.entries[i].albedo_r = 200;
			scene.palette.entries[i].albedo_g = 200;
			scene.palette.entries[i].albedo_b = 200;
		}
		scene.warnings.push_back("the file carries no RGBA palette chunk; every entry imported as light grey");
	}

	// max_emissive must be known before writing emissive bytes
	float brightest = 0.0f;
	for (const auto& [index, material] : materials) {
		brightest = std::max(brightest, material.emission);
	}
	scene.palette.max_emissive = brightest > 0.0f ? brightest : 1.0f;

	for (const auto& [index, material] : materials) {
		PaletteEntry& entry = scene.palette.entries[index];
		entry.roughness = voxByteFromUnit(material.roughness);
		entry.metallic = voxByteFromUnit(material.metallic);
		entry.emissive = voxByteFromUnit(material.emission / scene.palette.max_emissive);
		if (material.transparent) {
			entry.flags |= toast::voxel::k_entry_transparent;
		}
	}

	// A cycle has no root and would never finish flattening
	if (!node_order.empty()) {
		std::vector<int32_t> roots;
		for (int32_t id : node_order) {
			if (!raw_nodes[id].is_child) {
				roots.push_back(id);
			}
		}
		if (roots.size() != 1) {
			throw std::runtime_error(".vox: its scene graph has " + std::to_string(roots.size()) + " roots; it must have exactly one");
		}

		std::unordered_map<int32_t, uint32_t> remap;
		remap.insert_or_assign(roots[0], 0u);
		scene.nodes.emplace_back();
		for (int32_t id : node_order) {
			if (!remap.contains(id)) {
				remap.insert_or_assign(id, static_cast<uint32_t>(scene.nodes.size()));
				scene.nodes.emplace_back();
			}
		}

		for (const auto& [id, raw] : raw_nodes) {
			VoxNode& node = scene.nodes[remap.at(id)];
			node = raw.node;
			node.children.reserve(raw.child_ids.size());
			for (int32_t child_id : raw.child_ids) {
				const auto found = remap.find(child_id);
				if (found == remap.end()) {
					throw std::runtime_error(
					    ".vox: node " + std::to_string(id) + " references node " + std::to_string(child_id) +
					    ", which the file does not hold"
					);
				}
				node.children.push_back(found->second);
			}
		}
	}

	return scene;
}

}
