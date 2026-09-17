#include "vox_intermediates.hpp"

#include "prefab.hpp"
#include "vox_importer.h"    // ffi TOAST_C_API or the entry points are not exported
#include "voxel_model.hpp"
#include "voxel_palette.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <toast/log.hpp>
#include <toast/uid.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_set>

namespace assets {

using voxel::BrickPool;
using voxel::k_brick_dim;
using voxel::k_palette_size;
using voxel::k_voxel_size;
using voxel::LatticePlacement;
using voxel::PaletteEntry;
using voxel::Volume;

namespace {

[[nodiscard]]
auto voxSanitise(std::string_view name) -> std::string {
	std::string out;
	out.reserve(name.size());
	for (char c : name) {
		const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		out.push_back(safe ? c : '_');
	}

	const size_t first = out.find_first_not_of('_');
	if (first == std::string::npos) {
		return {};
	}
	return out.substr(first, out.find_last_not_of('_') - first + 1);
}

[[nodiscard]]
auto voxReadFile(const std::filesystem::path& path) -> std::vector<uint8_t> {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		throw std::runtime_error("cannot open " + path.string());
	}
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void voxWriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		throw std::runtime_error("cannot write " + path.string());
	}
	if (!bytes.empty()) {
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	if (!file) {
		throw std::runtime_error("failed while writing " + path.string());
	}
}

struct VoxNodeTransform {
	glm::vec3 position {0.0f};
	glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 scale {1.0f};
};

/// @brief Mirrored orientations have no quaternion so one axis gets a negative scale
[[nodiscard]]
auto voxTransformOf(const voxel::LatticeOrientation& orientation, glm::ivec3 offset) -> VoxNodeTransform {
	VoxNodeTransform out;
	out.position = glm::vec3(offset) * k_voxel_size;

	glm::mat3 matrix = toMatrix(orientation);
	if (orientation.isMirror()) {
		out.scale = glm::vec3(-1.0f, 1.0f, 1.0f);
		matrix[0] = -matrix[0];    // glm indexes [column][row]
	}
	out.rotation = glm::quat_cast(matrix);
	return out;
}

void voxWriteTransform(nlohmann::json& node, const VoxNodeTransform& transform) {
	node["position"] = {transform.position.x, transform.position.y, transform.position.z};
	node["rotation"] = {transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w};
	node["scale"] = {transform.scale.x, transform.scale.y, transform.scale.z};
}

}

auto writeVoxIntermediates(const std::filesystem::path& source, const std::filesystem::path& out_dir, uint64_t palette_uid)
    -> VoxIntermediates {
	ZoneScoped;

	const std::vector<uint8_t> bytes = voxReadFile(source);
	const VoxScene scene = importVox(bytes);

	std::filesystem::create_directories(out_dir);

	VoxIntermediates out;
	out.base_name = voxSanitise(source.stem().string());
	if (out.base_name.empty()) {
		out.base_name = "model";
	}
	out.warnings = scene.warnings;

	std::vector<std::string> model_names(scene.models.size());
	for (const VoxPlacement& placement : flattenVoxScene(scene)) {
		if (placement.model < model_names.size() && model_names[placement.model].empty()) {
			model_names[placement.model] = voxSanitise(placement.name);
		}
	}

	std::unordered_set<std::string> taken;
	out.models.reserve(scene.models.size());
	for (uint32_t i = 0; i < scene.models.size(); ++i) {
		std::string name = model_names[i].empty() ? "model" + std::to_string(i) : model_names[i];
		while (!taken.insert(name).second) {
			name += "_";
		}

		const VoxModel& model = scene.models[i];
		const glm::uvec3 bricks = glm::max((model.dims + (k_brick_dim - 1u)) / k_brick_dim, glm::uvec3(1));
		BrickPool pool(bricks.x * bricks.y * bricks.z);

		const std::optional<Volume> volume = buildVoxVolume(model, pool);
		if (!volume.has_value()) {
			throw std::runtime_error(
			    ".vox: model " + std::to_string(i) + " is too large to build a volume for (" + std::to_string(model.dims.x) + "x" +
			    std::to_string(model.dims.y) + "x" + std::to_string(model.dims.z) + ")"
			);
		}

		const std::unique_ptr<VoxelModel> asset = VoxelModel::capture(*volume, palette_uid);
		const std::string file_name = out.base_name + "_" + name + ".tvox";
		voxWriteFile(out_dir / file_name, asset->serialize(SaveMode::game));

		out.models.push_back(VoxIntermediateModel {.file_name = file_name, .model = i, .solid_voxels = asset->solidVoxelCount()});
	}

	std::vector<uint8_t> defaulted;
	for (uint32_t i = 1; i < k_palette_size; ++i) {
		if (!(scene.palette.entries[i] == PaletteEntry {})) {
			defaulted.push_back(static_cast<uint8_t>(i));
		}
	}
	const VoxelPalette palette(scene.palette, 0, std::move(defaulted));
	out.palette_file_name = out.base_name + ".tpal";
	voxWriteFile(out_dir / out.palette_file_name, palette.serialize(SaveMode::editor));

	nlohmann::json manifest;
	manifest["name"] = out.base_name;
	manifest["palette"] = out.palette_file_name;
	manifest["warnings"] = out.warnings;

	std::vector<std::string> file_by_model(scene.models.size());
	for (const VoxIntermediateModel& model : out.models) {
		file_by_model[model.model] = model.file_name;
	}

	// A chain of single child nodes collapses into its last node with the accumulated transform
	const std::function<nlohmann::json(uint32_t, const VoxTransform&, const std::string&, bool)> emit =
	    [&](uint32_t index, const VoxTransform& carried, const std::string& carried_name, bool carried_hidden) {
		    const VoxNode& node = scene.nodes[index];
		    const VoxTransform local = composeVoxTransforms(carried, node.local);
		    const std::string name = node.name.empty() ? carried_name : node.name;
		    const bool hidden = carried_hidden || node.hidden;

		    if (node.model.has_value()) {
			    nlohmann::json json;
			    json["name"] = name.empty() ? out.base_name + "_" + std::to_string(index) : name;
			    json["hidden"] = hidden;
			    json["type"] = "voxelNode";
			    json["model"] = file_by_model[*node.model];
			    json["mobility"] = 0;    // VoxelMobility::static_geometry since world/ cannot be included here
			    voxWriteTransform(
			        json, voxTransformOf(local.orientation, voxPlacementOf(local, scene.models[*node.model].dims).offset)
			    );
			    return json;
		    }

		    if (node.children.size() == 1) {
			    return emit(node.children[0], local, name, hidden);
		    }

		    nlohmann::json json;
		    json["name"] = name.empty() ? out.base_name + "_" + std::to_string(index) : name;
		    json["hidden"] = hidden;
		    json["type"] = "toast::Node3D";
		    voxWriteTransform(json, voxTransformOf(local.orientation, local.translation));

		    json["children"] = nlohmann::json::array();
		    for (uint32_t child : node.children) {
			    json["children"].push_back(emit(child, VoxTransform {}, "", false));
		    }
		    return json;
	    };

	manifest["nodes"] = nlohmann::json::array();
	if (scene.nodes.empty()) {
		for (uint32_t i = 0; i < scene.models.size(); ++i) {
			nlohmann::json json;
			json["name"] = out.base_name + (scene.models.size() == 1 ? "" : "_" + std::to_string(i));
			json["type"] = "voxelNode";
			json["hidden"] = false;
			json["model"] = file_by_model[i];
			json["mobility"] = 0;
			voxWriteTransform(
			    json, voxTransformOf(voxel::LatticeOrientation {}, voxPlacementOf(VoxTransform {}, scene.models[i].dims).offset)
			);
			manifest["nodes"].push_back(std::move(json));
		}
	} else {
		manifest["nodes"].push_back(emit(0, VoxTransform {}, "", false));
	}

	out.manifest_file_name = out.base_name + ".json";
	const std::string text = manifest.dump(2);
	voxWriteFile(out_dir / out.manifest_file_name, std::vector<uint8_t>(text.begin(), text.end()));
	return out;
}

void voxManifestToPrefab(const std::filesystem::path& manifest_path, const std::filesystem::path& out_path) {
	ZoneScoped;

	std::ifstream file(manifest_path);
	if (!file) {
		throw std::runtime_error("cannot open " + manifest_path.string());
	}
	const nlohmann::json manifest = nlohmann::json::parse(file);
	if (!manifest.contains("nodes") || !manifest["nodes"].is_array()) {
		throw std::runtime_error(manifest_path.string() + " is not a .vox manifest: it has no nodes");
	}

	const auto uid_of = [&](const nlohmann::json& node, const char* key) -> std::optional<toast::UID> {
		if (!node.contains(key) || !node[key].is_string()) {
			return std::nullopt;
		}
		const std::string value = node[key].get<std::string>();
		if (value.size() != 11) {
			TOAST_WARN("AssetManager", "vox manifest holds an unpatched {} reference '{}'; the node is written without it", key, value);
			return std::nullopt;
		}
		return toast::UID(toast::UID::fromString(value));
	};

	const std::optional<toast::UID> palette_uid = uid_of(manifest, "palette");

	Prefab prefab;
	const std::function<void(const nlohmann::json&, const std::string&)> walk = [&](const nlohmann::json& node,
	                                                                                const std::string& parent_uid) {
		Prefab::BasicNode basic;
		basic.name = node.value("name", "voxel");
		basic.type = node.value("type", "toast::Node3D");

		const toast::UID uid = toast::UID::make();
		const std::string uid_string = uid.get();
		basic.fields.push_back({"m_uid", toast::FieldType::uid_t, false, uid});

		basic.fields.push_back({"m_local_enabled", toast::FieldType::bool_t, false, !node.value("hidden", false)});
		if (!parent_uid.empty()) {
			basic.fields.push_back({"m_parent", toast::FieldType::uid_t, false, toast::UID(toast::UID::fromString(parent_uid))});
		}

		Prefab::Group transform;
		transform.name = "Transform";
		const auto vec3_of = [&node](const char* key, glm::vec3 fallback) {
			if (!node.contains(key) || !node[key].is_array() || node[key].size() != 3) {
				return fallback;
			}
			return glm::vec3(node[key][0].get<float>(), node[key][1].get<float>(), node[key][2].get<float>());
		};

		glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() == 4) {
			const auto& r = node["rotation"];
			rotation = glm::quat(r[3].get<float>(), r[0].get<float>(), r[1].get<float>(), r[2].get<float>());
		}

		// Field names must match the reflected Node3D ones since applyFields() silently skips unknown names
		transform.fields.push_back({"position", toast::FieldType::vec3_t, false, vec3_of("position", glm::vec3(0.0f))});
		transform.fields.push_back({"rotation", toast::FieldType::quaternion_t, false, rotation});
		transform.fields.push_back({"scale", toast::FieldType::vec3_t, false, vec3_of("scale", glm::vec3(1.0f))});
		basic.groups.push_back(std::move(transform));

		if (basic.type == "voxelNode") {
			if (const std::optional<toast::UID> model = uid_of(node, "model")) {
				basic.fields.push_back({"m_model", toast::FieldType::uid_t, false, *model});
			}
			if (palette_uid.has_value()) {
				basic.fields.push_back({"m_palette", toast::FieldType::uid_t, false, *palette_uid});
			}
			basic.fields.push_back({"m_mobility", toast::FieldType::int_t, false, node.value("mobility", 0)});
		}

		prefab.nodes.push_back(std::move(basic));

		if (node.contains("children")) {
			for (const nlohmann::json& child : node["children"]) {
				walk(child, uid_string);
			}
		}
	};

	for (const nlohmann::json& node : manifest["nodes"]) {
		walk(node, "");
	}

	std::ofstream out(out_path);
	if (!out) {
		throw std::runtime_error("cannot write " + out_path.string());
	}
	out << prefab.toFile();
	if (!out) {
		throw std::runtime_error("failed while writing " + out_path.string());
	}
}

}

extern "C" {

// noexcept entry points so exceptions must not reach the editor
void vox_generate_intermediates(const char* source_path, const char* out_dir, const char* palette_uid) noexcept {
	try {
		uint64_t uid = 0;
		if (palette_uid != nullptr && *palette_uid != '\0') {
			uid = toast::UID::fromString(palette_uid);
		}

		const assets::VoxIntermediates result =
		    assets::writeVoxIntermediates(std::filesystem::path(source_path), std::filesystem::path(out_dir), uid);
		for (const std::string& warning : result.warnings) {
			TOAST_WARN("AssetManager", "vox import: {}", warning);
		}
	} catch (const std::exception& e) { TOAST_ERROR("AssetManager", "vox import failed: {}", e.what()); } catch (...) {
		TOAST_ERROR("AssetManager", "vox import failed with an unrecognized exception");
	}
}

void vox_create_tnode(const char* manifest_path, const char* output_path) noexcept {
	try {
		assets::voxManifestToPrefab(std::filesystem::path(manifest_path), std::filesystem::path(output_path));
	} catch (const std::exception& e) {
		TOAST_ERROR("AssetManager", "vox scene-to-tnode conversion failed: {}", e.what());
	} catch (...) { TOAST_ERROR("AssetManager", "vox scene-to-tnode conversion failed with an unrecognized exception"); }
}
}
