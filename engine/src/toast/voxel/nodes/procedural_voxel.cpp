#include "procedural_voxel.hpp"

#include "voxel_node_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <ranges>
#include <toast/assets/asset_manager.hpp>
#include <toast/log.hpp>
#include <toast/renderer/editor_overlays.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/thread_pool.hpp>
#include <toast/voxel/assets/voxel_palette.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/world/node_owner.hpp>
#include <toast/world/workspace_events.hpp>
#include <tracy/Tracy.hpp>

namespace toast {

namespace {

using _detail::assetOfType;

void hashCombine(uint64_t& seed, uint64_t value) noexcept {
	// splitmix64 finaliser
	value += 0x9e3779b97f4a7c15ull + seed;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
	seed = value ^ (value >> 31);
}

void hashFloats(uint64_t& seed, const float* data, size_t count) noexcept {
	for (size_t i = 0; i < count; ++i) {
		uint32_t bits = 0;
		std::memcpy(&bits, &data[i], sizeof(bits));
		hashCombine(seed, bits);
	}
}

[[nodiscard]]
auto paletteAsset(uint64_t uid) -> const voxel::Palette* {
	if (uid == 0) {
		return nullptr;
	}
	const assets::Handle<assets::VoxelPalette> handle = assets::load<assets::VoxelPalette>(UID(uid));
	const auto* palette = assetOfType(handle, "voxel_palette");
	return palette != nullptr ? &palette->palette() : nullptr;
}

/// snapped to the closest voxel and right angle when it is off
[[nodiscard]]
auto snappedPlacement(const glm::mat4& to_shape) -> std::pair<voxel::LatticePlacement, bool> {
	if (std::optional<voxel::LatticePlacement> exact = voxel::placementFromTransform(to_shape)) {
		return {*exact, true};
	}
	const glm::vec3 translation = glm::vec3(to_shape[3]) / voxel::k_voxel_size;
	return {
	  voxel::LatticePlacement {
	                           .orientation = voxel::snapOrientation(glm::mat3(to_shape)), .offset = glm::ivec3(glm::round(translation))
	  },
	  false
	};
}

[[nodiscard]]
auto landedBox(glm::ivec3 size, const voxel::LatticePlacement& placement) -> voxel::EditBounds {
	voxel::EditBounds out;
	for (int axis = 0; axis < 3; ++axis) {
		const int32_t extent = size[placement.orientation.source[static_cast<size_t>(axis)]];
		out.min[axis] =
		    placement.orientation.flip[static_cast<size_t>(axis)] ? placement.offset[axis] - extent : placement.offset[axis];
		out.max[axis] = out.min[axis] + extent - 1;
	}
	return out;
}

}

auto owningProceduralVoxel(Node& node) -> ProceduralVoxel* {
	Box<Node> current = node.parent();
	while (current.exists()) {
		if (auto* shape = reflect_cast<ProceduralVoxel>(&*current)) {
			return shape;
		}
		// Groups hold pieces and a slice keeps its other half as a child of the kept one
		if (reflect_cast<VoxelGroup>(&*current) == nullptr && reflect_cast<VoxelPiece>(&*current) == nullptr) {
			return nullptr;
		}
		current = current->parent();
	}
	return nullptr;
}

auto parentToShape(Node& node) -> glm::mat4 {
	glm::mat4 out(1.0f);
	Box<Node> current = node.parent();
	while (current.exists() && reflect_cast<ProceduralVoxel>(&*current) == nullptr) {
		if (auto* spatial = reflect_cast<Node3D>(&*current)) {
			spatial->syncTransform();
			out = spatial->getTransform() * out;
		}
		current = current->parent();
	}
	return out;
}

auto piecePlacement(VoxelPiece& piece) -> voxel::LatticePlacement {
	piece.syncTransform();
	return snappedPlacement(parentToShape(piece) * piece.getTransform()).first;
}

auto pieceBounds(VoxelPiece& piece) -> voxel::EditBounds {
	return landedBox(piece.nominalSize(), piecePlacement(piece));
}

void placePiece(VoxelPiece& piece, const voxel::LatticeOrientation& orientation, glm::ivec3 min, glm::ivec3 max) {
	const glm::ivec3 lo = glm::min(min, max);
	glm::ivec3 hi = glm::max(min, max);

	if (auto* volume = reflect_cast<VoxelVolume>(&piece)) {
		glm::ivec3 local_size;
		for (int axis = 0; axis < 3; ++axis) {
			local_size[orientation.source[static_cast<size_t>(axis)]] = hi[axis] - lo[axis] + 1;
		}
		volume->setSizeVoxels(local_size);
	} else {
		// A mesh keeps the size of its model so only its corner follows
		const glm::ivec3 size = piece.nominalSize();
		for (int axis = 0; axis < 3; ++axis) {
			hi[axis] = lo[axis] + size[orientation.source[static_cast<size_t>(axis)]] - 1;
		}
	}

	glm::ivec3 offset;
	for (int axis = 0; axis < 3; ++axis) {
		offset[axis] = orientation.flip[static_cast<size_t>(axis)] ? hi[axis] + 1 : lo[axis];
	}

	glm::mat4 wanted(voxel::toMatrix(orientation));
	wanted[3] = glm::vec4(glm::vec3(offset) * voxel::k_voxel_size, 1.0f);
	const glm::mat4 local = glm::inverse(parentToShape(piece)) * wanted;
	piece.position = glm::vec3(local[3]);
	piece.rotation = glm::normalize(glm::quat_cast(glm::mat3(local)));
	piece.syncTransform();
}

auto planeToShape(glm::vec4 plane, const voxel::LatticePlacement& placement) noexcept -> glm::vec4 {
	glm::vec3 normal;
	for (int axis = 0; axis < 3; ++axis) {
		const float along = plane[placement.orientation.source[static_cast<size_t>(axis)]];
		normal[axis] = placement.orientation.flip[static_cast<size_t>(axis)] ? -along : along;
	}
	return {normal, plane.w - glm::dot(normal, glm::vec3(placement.offset))};
}

auto planeToPiece(glm::vec4 plane, const voxel::LatticePlacement& placement) noexcept -> glm::vec4 {
	glm::vec3 normal;
	for (int axis = 0; axis < 3; ++axis) {
		const float along = placement.orientation.flip[static_cast<size_t>(axis)] ? -plane[axis] : plane[axis];
		normal[placement.orientation.source[static_cast<size_t>(axis)]] = along;
	}
	return {normal, plane.w + glm::dot(glm::vec3(plane), glm::vec3(placement.offset))};
}

auto insideProceduralVoxel(Node& node) -> bool {
	return owningProceduralVoxel(node) != nullptr;
}

// ProceduralVoxel

struct ProceduralVoxel::ComposeInput {
	struct Preview {
		VoxelPiece::GridRef grid;
		uint8_t kind = 0;
		voxel::EditBounds box;
		voxel::WriteMode mode = voxel::WriteMode::replace;    ///< How a fill preview lands
	};

	std::vector<Landing> landings;
	std::vector<std::pair<glm::ivec3, uint8_t>> buckets;
	std::unique_ptr<voxel::Volume> base;    ///< The model the shape starts from
	std::optional<Preview> preview;
	std::optional<size_t> highlight;        ///< The landing of the selected piece
	bool projections = false;
	voxel::Palette palette;
};

struct ProceduralVoxel::ComposeOutput {
	std::unique_ptr<voxel::Volume> volume;
	std::unique_ptr<voxel::Volume> highlight;
	glm::ivec3 origin {0};
	std::vector<event::VoxelProjectionData> projections;
	bool has_projections = false;
	bool pool_exhausted = false;
};

namespace {

struct ProjectionAxes {
	int h;
	int v;
	int depth;
	int toward;
};

constexpr std::array<ProjectionAxes, 3> k_projection_views {
  ProjectionAxes {0, 2, 1,  1},
   ProjectionAxes {1, 2, 0, -1},
   ProjectionAxes {0, 1, 2, -1}
};

void projectShape(
    const voxel::Volume& shape, glm::ivec3 origin, const voxel::Palette& palette, std::vector<event::VoxelProjectionData>& out
) {
	ZoneScoped;
	const glm::ivec3 dims = glm::ivec3(shape.voxelDims());

	for (const ProjectionAxes& view : k_projection_views) {
		event::VoxelProjectionData projection;
		projection.min_h = origin[view.h];
		projection.min_v = origin[view.v];
		projection.width = static_cast<uint32_t>(dims[view.h]);
		projection.height = static_cast<uint32_t>(dims[view.v]);
		projection.colors.assign(static_cast<size_t>(projection.width) * projection.height * 4, 0);
		projection.depths.assign(static_cast<size_t>(projection.width) * projection.height, std::numeric_limits<int16_t>::max());
		projection.edges_h.assign(static_cast<size_t>(projection.width + 1) * projection.height * 4, 0);
		projection.edges_v.assign(static_cast<size_t>(projection.width) * (projection.height + 1) * 4, 0);
		out.push_back(std::move(projection));
	}

	// Outlines
	for (uint32_t index = 0; index < shape.brickCount(); ++index) {
		const glm::ivec3 brick = shape.brickAtIndex(index);
		const voxel::BrickEntry entry = shape.entryAt(brick);
		if (entry.tag() == voxel::BrickTag::empty) {
			continue;
		}
		const voxel::BrickOccupancy& occupancy = *shape.occupancyPointer(brick);
		const voxel::BrickNeighbourhood around = shape.neighbourhoodOf(brick);
		// exposed[axis][0] has a voxel when the one before it on axis is empty
		const std::array<std::array<voxel::BrickOccupancy, 2>, 3> exposed {
		  {
		   {occupancy & ~voxel::neighboursNegX(occupancy, around.neg_x),
			   occupancy & ~voxel::neighboursPosX(occupancy, around.pos_x)},
		   {occupancy & ~voxel::neighboursNegY(occupancy, around.neg_y),
			   occupancy & ~voxel::neighboursPosY(occupancy, around.pos_y)},
		   {occupancy & ~voxel::neighboursNegZ(occupancy, around.neg_z),
			   occupancy & ~voxel::neighboursPosZ(occupancy, around.pos_z)},
		   }
		};
		const bool uniform = entry.tag() == voxel::BrickTag::uniform;
		const uint8_t* materials = uniform ? nullptr : std::as_const(*shape.pool()).material(entry.payload()).data();
		const glm::ivec3 corner = brick * static_cast<int32_t>(voxel::k_brick_dim);

		for (size_t v = 0; v < k_projection_views.size(); ++v) {
			event::VoxelProjectionData& projection = out[v];
			const ProjectionAxes& view = k_projection_views[v];
			const auto& across = exposed[static_cast<size_t>(view.h)];
			const auto& up = exposed[static_cast<size_t>(view.v)];
			for (uint32_t z = 0; z < voxel::k_brick_dim; ++z) {
				uint64_t bits = across[0][z] | across[1][z] | up[0][z] | up[1][z];
				while (bits != 0) {
					const auto bit = static_cast<uint32_t>(std::countr_zero(bits));
					const uint64_t mask = uint64_t {1} << bit;
					bits &= bits - 1;

					const uint32_t local = (z * 64) + bit;
					const uint8_t id = uniform ? static_cast<uint8_t>(entry.payload()) : materials[local];
					const voxel::PaletteEntry& color = palette.entries[id];
					const glm::ivec3 cell =
					    corner + glm::ivec3(static_cast<int32_t>(bit & 7), static_cast<int32_t>(bit >> 3), static_cast<int32_t>(z));
					const auto h = static_cast<size_t>(cell[view.h]);
					const auto row = static_cast<size_t>(cell[view.v]);

					// See through
					const auto mark = [&](std::vector<uint8_t>& edges, size_t at) {
						edges[(at * 4) + 0] = color.albedo_r;
						edges[(at * 4) + 1] = color.albedo_g;
						edges[(at * 4) + 2] = color.albedo_b;
						edges[(at * 4) + 3] = 255;
					};
					if ((across[0][z] & mask) != 0) {
						mark(projection.edges_h, h + (row * (projection.width + 1)));
					}
					if ((across[1][z] & mask) != 0) {
						mark(projection.edges_h, h + 1 + (row * (projection.width + 1)));
					}
					if ((up[0][z] & mask) != 0) {
						mark(projection.edges_v, h + (row * projection.width));
					}
					if ((up[1][z] & mask) != 0) {
						mark(projection.edges_v, h + ((row + 1) * projection.width));
					}
				}
			}
		}
	}

	// See through fill
	constexpr float k_layer_alpha = 0.1f;
	constexpr int32_t k_dim = static_cast<int32_t>(voxel::k_brick_dim);
	for (size_t v = 0; v < k_projection_views.size(); ++v) {
		event::VoxelProjectionData& projection = out[v];
		const ProjectionAxes& view = k_projection_views[v];
		const int length = dims[view.depth];
		for (uint32_t row = 0; row < projection.height; ++row) {
			for (uint32_t h = 0; h < projection.width; ++h) {
				glm::vec3 color(0.0f);
				float alpha = 0.0f;
				uint8_t previous = voxel::k_empty_palette_index;
				int nearest = -1;
				glm::ivec3 cell;
				cell[view.h] = static_cast<int>(h);
				cell[view.v] = static_cast<int>(row);
				const auto layer = [&](uint8_t id) {
					const voxel::PaletteEntry& entry = palette.entries[id];
					const glm::vec3 tint = glm::vec3(entry.albedo_r, entry.albedo_g, entry.albedo_b) / 255.0f;
					color = tint * k_layer_alpha + color * (1.0f - k_layer_alpha);
					alpha = k_layer_alpha + (alpha * (1.0f - k_layer_alpha));
				};
				int step = 0;
				while (step < length) {
					cell[view.depth] = view.toward > 0 ? length - 1 - step : step;
					const voxel::BrickEntry entry = shape.entryAt({cell.x >> 3, cell.y >> 3, cell.z >> 3});
					// Empty and uniform bricks are one run, the march jumps to the next brick
					const int along = cell[view.depth] & (k_dim - 1);
					const int rest = view.toward > 0 ? along + 1 : k_dim - along;
					if (entry.tag() == voxel::BrickTag::empty) {
						step += rest;
						previous = voxel::k_empty_palette_index;
						continue;
					}
					if (entry.tag() == voxel::BrickTag::uniform) {
						const auto id = static_cast<uint8_t>(entry.payload());
						if (id != previous) {
							layer(id);
						}
						previous = id;
						nearest = view.toward > 0 ? cell[view.depth] - (rest - 1) : cell[view.depth] + (rest - 1);
						step += rest;
						continue;
					}
					const uint8_t id = shape.materialAt(cell);
					if (id != voxel::k_empty_palette_index) {
						if (id != previous) {
							layer(id);
						}
						nearest = cell[view.depth];
					}
					previous = id;
					++step;
				}
				if (alpha <= 0.0f) {
					continue;
				}
				// color is premultiplied
				const size_t slot = static_cast<size_t>(h) + (static_cast<size_t>(row) * projection.width);
				for (int c = 0; c < 3; ++c) {
					projection.colors[(slot * 4) + c] =
					    static_cast<uint8_t>(std::lround(glm::clamp(color[c] / alpha, 0.0f, 1.0f) * 255.0f));
				}
				projection.colors[(slot * 4) + 3] = static_cast<uint8_t>(std::lround(alpha * 255.0f));
				projection.depths[slot] = static_cast<int16_t>(view.toward * (nearest + origin[view.depth]));
			}
		}
	}
}

[[nodiscard]]
auto brickFloor(glm::ivec3 voxel) noexcept -> glm::ivec3 {
	return {voxel.x >> 3, voxel.y >> 3, voxel.z >> 3};
}

}

void ProceduralVoxel::rebuild() {
	if (buildsAsync()) {
		m_compose_wanted = true;
		return;
	}
	requestRebuild();
	refreshVolume();
}

void ProceduralVoxel::collect(Node& node, const glm::mat4& to_root, std::vector<Collected>& out) {
	for (const Box<Node>& child_box : node.children()) {
		if (!child_box.exists() || !child_box->enabled()) {
			continue;
		}
		Node& child = const_cast<Node&>(*child_box);
		glm::mat4 child_to_root = to_root;
		if (auto* spatial = reflect_cast<Node3D>(&child)) {
			// Edits land in the fields first and the matrix only catches up on sync
			spatial->syncTransform();
			child_to_root = to_root * spatial->getTransform();
		}
		if (auto* piece = reflect_cast<VoxelPiece>(&child)) {
			out.push_back({piece, child_to_root});
			// A piece can hold pieces too, they land right after it
			collect(child, child_to_root, out);
		} else if (reflect_cast<VoxelGroup>(&child) != nullptr) {
			collect(child, child_to_root, out);
		}
	}
}

void ProceduralVoxel::collectBuckets(Node& node, const glm::mat4& to_root, std::vector<BucketFill>& out) {
	for (const Box<Node>& child_box : node.children()) {
		if (!child_box.exists() || !child_box->enabled()) {
			continue;
		}
		Node& child = const_cast<Node&>(*child_box);
		auto* spatial = reflect_cast<Node3D>(&child);
		glm::mat4 child_to_root = to_root;
		if (spatial != nullptr) {
			spatial->syncTransform();
			child_to_root = to_root * spatial->getTransform();
		}
		if (auto* bucket = reflect_cast<VoxelBucket>(&child)) {
			// The bucket sits in the voxel it fills from
			out.push_back({bucket, glm::ivec3(glm::floor(glm::vec3(child_to_root[3]) / voxel::k_voxel_size))});
		} else if (reflect_cast<VoxelGroup>(&child) != nullptr || reflect_cast<VoxelPiece>(&child) != nullptr) {
			collectBuckets(child, child_to_root, out);
		}
	}
}

auto ProceduralVoxel::toolPiece() -> VoxelPiece* {
	const renderer::EditorOverlays& overlays = renderer::editorOverlays();
	// An outline only preview draws nothing into the shape
	if (!overlays.tool_box_active || overlays.tool_root != uid() || overlays.tool_box_kind > 2) {
		return nullptr;
	}
	return overlays.tool_piece;
}

auto ProceduralVoxel::keyOf(const std::vector<Collected>& pieces) -> uint64_t {
	uint64_t key = 0x70726f63u;
	hashCombine(key, paletteUid());
	for (const Collected& entry : pieces) {
		hashCombine(key, entry.piece->uid().data());
		hashCombine(key, reinterpret_cast<uintptr_t>(entry.piece->info()));
		hashCombine(key, entry.piece->inputRevision());
		hashFloats(key, &entry.to_root[0][0], 16);
	}

	std::vector<BucketFill> buckets;
	collectBuckets(*this, glm::mat4(1.0f), buckets);
	for (const BucketFill& fill : buckets) {
		hashCombine(key, fill.bucket->uid().data());
		hashCombine(key, fill.bucket->colorId());
		hashCombine(
		    key,
		    static_cast<uint64_t>(static_cast<uint32_t>(fill.seed.x)) ^
		        (static_cast<uint64_t>(static_cast<uint32_t>(fill.seed.y)) << 21) ^
		        (static_cast<uint64_t>(static_cast<uint32_t>(fill.seed.z)) << 42)
		);
	}

	// The editor previews tools inside the shape and tints the selection
	if (!participatesIn(NodeOwnerParticipation::gameplay_tick)) {
		const renderer::EditorOverlays& overlays = renderer::editorOverlays();
		if (VoxelPiece* tool = toolPiece()) {
			hashCombine(key, overlays.tool_box_kind);
			hashCombine(key, tool->inputRevision());
			const std::array<glm::vec3, 2> corners = {glm::vec3(overlays.tool_box_min), glm::vec3(overlays.tool_box_max)};
			hashFloats(key, &corners[0].x, 6);
		}
	}
	return key;
}

auto ProceduralVoxel::shapeKey() -> uint64_t {
	std::vector<Collected> pieces;
	collect(*this, glm::mat4(1.0f), pieces);
	return keyOf(pieces);
}

auto ProceduralVoxel::remapFor(const VoxelPiece& piece) -> voxel::PaletteRemapTable {
	const uint64_t source_uid = piece.sourcePaletteUid();
	if (source_uid == 0 || source_uid == paletteUid()) {
		return voxel::identityRemap();
	}
	const voxel::Palette* target = resolvedPalette();
	const voxel::Palette* source = paletteAsset(source_uid);
	if (target == nullptr || source == nullptr) {
		return voxel::identityRemap();
	}

	CachedRemap& cached = m_remaps[source_uid];
	if (cached.target_uid == paletteUid() && cached.source_revision == source->revision &&
	    cached.target_revision == target->revision) {
		return cached.table;
	}
	voxel::PaletteRemap built = voxel::buildRemap(*source, *target);
	if (!built.unmatched.empty()) {
		TOAST_WARN("Voxel", "'{}' skips {} color(s) of '{}' missing from this palette", name(), built.unmatched.size(), piece.name());
	}
	cached = {
	  .table = built.table, .target_uid = paletteUid(), .source_revision = source->revision, .target_revision = target->revision
	};
	return cached.table;
}

auto ProceduralVoxel::land(const std::vector<Collected>& pieces, bool wait) -> std::vector<Landing> {
	std::vector<Landing> landings;
	landings.reserve(pieces.size());
	for (const Collected& entry : pieces) {
		const auto [placement, exact] = snappedPlacement(entry.to_root);
		entry.piece->setMisaligned(!exact);
		VoxelPiece::GridRef grid = wait ? entry.piece->gridRef() : entry.piece->readyGrid();
		if (grid == nullptr) {
			continue;
		}
		landings.push_back({
		  .piece = entry.piece,
		  .grid = std::move(grid),
		  .placement = placement,
		  .remap = remapFor(*entry.piece),
		  .kind = entry.piece->kind(),
		  .mode = entry.piece->writeMode(),
		  .match_id = entry.piece->matchId(),
		});
	}
	return landings;
}

auto ProceduralVoxel::apply(voxel::Volume& target, const Landing& landing, glm::ivec3 origin) -> voxel::EditResult {
	voxel::LatticePlacement indexed = landing.placement;
	indexed.offset -= origin;
	const voxel::Volume& grid = *landing.grid->volume;
	switch (landing.kind) {
		case PieceKind::fill: return voxel::stampVolume(target, grid, indexed, landing.remap, landing.mode, landing.match_id);
		case PieceKind::carve: return voxel::carveVolume(target, grid, indexed);
		case PieceKind::paint: return voxel::stampVolume(target, grid, indexed, landing.remap, voxel::WriteMode::solid_only, 0);
	}
	return {};
}

auto ProceduralVoxel::averageColor(const Landing& landing) -> glm::vec3 {
	const voxel::Palette& palette = resolvedPalette() != nullptr ? *resolvedPalette() : voxel::defaultPalette();
	const std::array<uint32_t, voxel::k_palette_size>& counts = landing.grid->histogram;
	glm::dvec3 sum {0.0};
	uint64_t total = 0;
	for (uint32_t id = 1; id < voxel::k_palette_size; ++id) {
		const uint8_t mapped = landing.remap[id];
		if (counts[id] == 0 || mapped == voxel::k_empty_palette_index) {
			continue;
		}
		const voxel::PaletteEntry& entry = palette.entries[mapped];
		sum += glm::dvec3(entry.albedo_r, entry.albedo_g, entry.albedo_b) * static_cast<double>(counts[id]);
		total += counts[id];
	}
	return total > 0 ? glm::vec3(sum / (255.0 * static_cast<double>(total))) : glm::vec3(0.5f);
}

auto ProceduralVoxel::layoutOf(const std::vector<Landing>& landings) -> std::vector<PieceLayout> {
	std::vector<PieceLayout> out;
	out.reserve(landings.size());
	for (const Landing& landing : landings) {
		out.push_back({
		  .node = landing.piece->box(),
		  .kind = landing.kind,
		  .placement = landing.placement,
		  .bounds = landedBox(landing.piece->nominalSize(), landing.placement),
		  .average_color = glm::vec4(averageColor(landing), 1.0f),
		});
	}
	return out;
}

void ProceduralVoxel::buildShape() {
	if (buildsAsync()) {
		m_compose_wanted = true;
		return;
	}

	ZoneScopedN("voxel::ComposeProcedural");    // NOLINT
	ZoneNameF("%.*s", static_cast<int>(name().size()), name().data());

	std::vector<Collected> pieces;
	collect(*this, glm::mat4(1.0f), pieces);
	std::vector<Landing> landings = land(pieces, true);

	voxel::EditBounds grows {};
	for (const Landing& landing : landings) {
		if (landing.kind == PieceKind::fill) {
			grows = voxel::boundsUnion(grows, voxel::placedBounds(landing.grid->volume->brickDims(), landing.placement));
		}
	}

	voxel::Volume* target = grows.empty() ? volume() : ensureContains(grows);
	m_layout = layoutOf(landings);
	if (target != nullptr) {
		for (const Landing& landing : landings) {
			commitEdit(apply(*target, landing, voxelOrigin()), "build");
		}

		// Buckets paint the finished shape so they never depend on the order of the pieces
		std::vector<BucketFill> buckets;
		collectBuckets(*this, glm::mat4(1.0f), buckets);
		for (const BucketFill& fill : buckets) {
			commitEdit(voxel::floodFill(*target, toIndex(fill.seed), fill.bucket->colorId()), "bucket");
		}
	}

	// In game the shape is only built once and destruction owns it after that
	if (!m_keep_grids) {
		for (const Landing& landing : landings) {
			landing.piece->releaseGrid();
		}
	}
}

auto ProceduralVoxel::buildsAsync() -> bool {
	// Only a workspace open for editing
	return owner() != nullptr && owner()->isEditing();
}

void ProceduralVoxel::tickAsyncBuild() {
	ZoneScopedN("voxel::TickProcedural");    // NOLINT
	const renderer::EditorOverlays& overlays = renderer::editorOverlays();

	std::vector<Collected> pieces;
	collect(*this, glm::mat4(1.0f), pieces);
	for (const Collected& entry : pieces) {
		entry.piece->collectGrid();
		entry.piece->requestGrid();
	}
	if (VoxelPiece* tool = toolPiece()) {
		tool->collectGrid();
		tool->requestGrid();
	}

	if (const uint64_t key = keyOf(pieces); key != m_shape_key || m_rebuild_requested) {
		m_shape_key = key;
		m_rebuild_requested = false;
		m_compose_wanted = true;
	}
	const bool projections_missing =
	    overlays.projections && (m_last_compose == nullptr || !m_last_compose->has_projections) && !m_pending_projections;
	if (projections_missing || overlays.selected != m_highlight_for) {
		m_compose_wanted = true;
	}

	if (m_compose_job.valid() && m_compose_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		std::shared_ptr<ComposeOutput> output = m_compose_job.get();
		m_pending_projections = false;
		applyComposed(*output);
		m_last_compose = std::move(output);
		sendLayout();
		finishShape();
	}

	// Only one compose runs at a time
	if (m_compose_wanted && !m_compose_job.valid()) {
		launchCompose(pieces);
	}
}

void ProceduralVoxel::launchCompose(const std::vector<Collected>& pieces) {
	ZoneScopedN("voxel::LaunchCompose");    // NOLINT
	m_compose_wanted = false;
	const renderer::EditorOverlays& overlays = renderer::editorOverlays();

	auto input = std::make_shared<ComposeInput>();
	input->landings = land(pieces, false);
	m_pending_layout = layoutOf(input->landings);

	// The selected piece gets tinted where it still shows
	m_highlight_for = overlays.selected;
	for (size_t i = 0; i < input->landings.size(); ++i) {
		const Landing& landing = input->landings[i];
		if (landing.kind != PieceKind::carve && landing.piece->uid() == m_highlight_for) {
			input->highlight = i;
		}
	}

	std::vector<BucketFill> buckets;
	collectBuckets(*this, glm::mat4(1.0f), buckets);
	for (const BucketFill& fill : buckets) {
		input->buckets.emplace_back(fill.seed, fill.bucket->colorId());
	}

	// The preview draws with the script the new volume will get
	if (VoxelPiece* tool = toolPiece()) {
		if (VoxelPiece::GridRef grid = tool->readyGrid()) {
			input->preview = ComposeInput::Preview {
			  .grid = std::move(grid),
			  .kind = overlays.tool_box_kind,
			  .box = {glm::min(overlays.tool_box_min, overlays.tool_box_max), glm::max(overlays.tool_box_min, overlays.tool_box_max)},
			  .mode = tool->writeMode(),
			};
		}
	}

	if (const assets::VoxelModel* model = resolvedModel()) {
		if (std::optional<voxel::Volume> instance = model->instantiate(voxel::proceduralBrickPool())) {
			input->base = std::make_unique<voxel::Volume>(std::move(*instance));
		}
	}

	input->projections = overlays.projections;
	input->palette = resolvedPalette() != nullptr ? *resolvedPalette() : voxel::defaultPalette();
	m_pending_projections = input->projections;

	m_compose_job = ThreadPool::push([input] { return compose(*input); });
}

auto ProceduralVoxel::compose(const ComposeInput& input) -> std::shared_ptr<ComposeOutput> {
	ZoneScopedN("voxel::ComposeProcedural");    // NOLINT
	auto out = std::make_shared<ComposeOutput>();
	out->has_projections = input.projections;

	voxel::EditBounds grows {};
	if (input.base != nullptr) {
		grows = {glm::ivec3(0), glm::ivec3(input.base->voxelDims()) - 1};
	}
	for (const Landing& landing : input.landings) {
		if (landing.kind == PieceKind::fill) {
			grows = voxel::boundsUnion(grows, voxel::placedBounds(landing.grid->volume->brickDims(), landing.placement));
		}
	}
	// Fill grows the shape like a real volume would
	if (input.preview.has_value() && input.preview->kind == 0) {
		grows = voxel::boundsUnion(grows, input.preview->box);
	}
	if (grows.empty()) {
		return out;
	}

	const glm::ivec3 first = brickFloor(grows.min);
	const glm::ivec3 last = brickFloor(grows.max);
	out->origin = first * static_cast<int32_t>(voxel::k_brick_dim);
	out->volume = std::make_unique<voxel::Volume>(voxel::proceduralBrickPool(), glm::uvec3(last - first + 1));
	voxel::Volume& target = *out->volume;

	voxel::EditResult result;
	if (input.base != nullptr) {
		const voxel::LatticePlacement at_zero {.offset = -out->origin};
		result += voxel::stampVolume(target, *input.base, at_zero, voxel::identityRemap(), voxel::WriteMode::replace, 0);
	}
	for (const Landing& landing : input.landings) {
		result += apply(target, landing, out->origin);
	}

	for (const auto& [seed, id] : input.buckets) {
		result += voxel::floodFill(target, seed - out->origin, id);
	}

	if (input.preview.has_value()) {
		const voxel::Volume& grid = *input.preview->grid->volume;
		const voxel::LatticePlacement placement {.offset = input.preview->box.min - out->origin};
		switch (input.preview->kind) {
			case 0: result += voxel::stampVolume(target, grid, placement, voxel::identityRemap(), input.preview->mode, 0); break;
			case 1: result += voxel::carveVolume(target, grid, placement); break;
			default:
				result += voxel::stampVolume(target, grid, placement, voxel::identityRemap(), voxel::WriteMode::solid_only, 0);
				break;
		}
	}

	if (input.highlight.has_value()) {
		ZoneScopedN("voxel::ProceduralHighlight");    // NOLINT
		const Landing& selected = input.landings[*input.highlight];
		out->highlight = std::make_unique<voxel::Volume>(voxel::proceduralBrickPool(), target.brickDims());
		voxel::Volume& highlight = *out->highlight;
		voxel::LatticePlacement indexed = selected.placement;
		indexed.offset -= out->origin;
		voxel::stampVolume(highlight, *selected.grid->volume, indexed, voxel::identityRemap(), voxel::WriteMode::replace, 0);

		// Only where the finished shape still has voxels
		for (uint32_t index = 0; index < highlight.brickCount(); ++index) {
			const glm::ivec3 brick = highlight.brickAtIndex(index);
			const voxel::BrickOccupancy* mine = highlight.occupancyPointer(brick);
			if (mine == nullptr) {
				continue;
			}
			const voxel::BrickOccupancy* shown = target.occupancyPointer(brick);
			if (shown == nullptr) {
				highlight.setBrickUniform(brick, voxel::k_empty_palette_index);
				continue;
			}
			const voxel::BrickOccupancy hidden = *mine & ~*shown;
			if (voxel::isEmpty(hidden)) {
				continue;
			}
			const std::optional<voxel::Volume::WritableBrick> write = highlight.beginBrickWrite(brick);
			if (!write.has_value()) {
				continue;
			}
			for (uint32_t z = 0; z < voxel::k_brick_dim; ++z) {
				uint64_t bits = hidden[z];
				while (bits != 0) {
					const auto bit = static_cast<uint32_t>(std::countr_zero(bits));
					write->material[(z * 64) + bit] = voxel::k_empty_palette_index;
					bits &= bits - 1;
				}
				(*write->occupancy)[z] &= ~hidden[z];
			}
			highlight.finishBrickWrite(brick, true);
		}
	}

	if (input.projections) {
		projectShape(target, out->origin, input.palette, out->projections);
	}
	out->pool_exhausted = result.pool_exhausted;
	return out;
}

void ProceduralVoxel::applyComposed(ComposeOutput& output) {
	ZoneScopedN("voxel::ApplyProcedural");    // NOLINT
	if (output.pool_exhausted) {
		TOAST_WARN(
		    "Voxel",
		    "'{}' is missing voxels: the procedural brick pool is out of its {} bricks",
		    name(),
		    voxel::k_procedural_brick_capacity
		);
	}
	renderer::VulkanRenderer* renderer = renderer::VulkanRenderer::instance;

	voxel::Volume* target = nullptr;
	{
		std::scoped_lock lock(voxel::runtimePoolMutex());
		target = volume();
		if (output.volume != nullptr) {
			const glm::ivec3 wanted_min = output.origin;
			const glm::ivec3 wanted_max = output.origin + glm::ivec3(output.volume->voxelDims()) - 1;
			const auto fits = [&](const voxel::Volume* current) {
				return current != nullptr && glm::all(glm::greaterThanEqual(wanted_min, voxelOrigin())) &&
				       glm::all(glm::lessThan(wanted_max, voxelOrigin() + glm::ivec3(current->voxelDims())));
			};
			if (!fits(target)) {
				// Growing makes a new volume the renderer uploads whole
				const glm::ivec3 slack =
				    glm::max(glm::ivec3(2 * static_cast<int32_t>(voxel::k_brick_dim)), (wanted_max - wanted_min + 1) / 4);
				voxel::Volume* grown = replaceVolume({wanted_min - slack, wanted_max + slack});
				if (grown == nullptr) {
					grown = replaceVolume({wanted_min, wanted_max});
				}
				if (grown != nullptr) {
					target = grown;
				} else {
					TOAST_WARN("Voxel", "'{}' is too big for one volume, it is clipped to the current one", name());
				}
			}
		}

		if (target != nullptr) {
			if (m_highlight != nullptr && m_highlight->brickDims() != target->brickDims()) {
				if (renderer != nullptr) {
					renderer->setVoxelHighlight(this, nullptr);
				}
				m_highlight.reset();
			}
			if (m_highlight == nullptr && output.highlight != nullptr) {
				m_highlight = std::make_unique<voxel::Volume>(voxel::runtimeBrickPool(), target->brickDims());
			}

			// Both origins sit on bricks
			const glm::ivec3 shift = (voxelOrigin() - output.origin) / static_cast<int32_t>(voxel::k_brick_dim);
			bool changed = false;
			for (uint32_t index = 0; index < target->brickCount(); ++index) {
				const glm::ivec3 brick = target->brickAtIndex(index);
				const glm::ivec3 source = brick + shift;
				const bool inside = output.volume != nullptr && output.volume->containsBrick(source);
				if (inside) {
					changed = target->copyBrickFrom(brick, *output.volume, source) || changed;
				} else if (target->entryAt(brick).tag() != voxel::BrickTag::empty) {
					target->setBrickUniform(brick, voxel::k_empty_palette_index);
					changed = true;
				}
				if (m_highlight != nullptr) {
					if (inside && output.highlight != nullptr) {
						m_highlight->copyBrickFrom(brick, *output.highlight, source);
					} else if (m_highlight->entryAt(brick).tag() != voxel::BrickTag::empty) {
						m_highlight->setBrickUniform(brick, voxel::k_empty_palette_index);
					}
				}
			}
			if (changed) {
				++m_revision;
				m_split_pending = true;
			}
		} else if (m_highlight != nullptr) {
			if (renderer != nullptr) {
				renderer->setVoxelHighlight(this, nullptr);
			}
			m_highlight.reset();
		}
	}
	if (renderer != nullptr && m_highlight != nullptr) {
		renderer->setVoxelHighlight(this, m_highlight.get());
	}

	// Only the projections are kept for the layout
	// The bricks go back to the pool
	output.volume.reset();
	output.highlight.reset();
	m_layout = std::move(m_pending_layout);
	m_pending_layout.clear();
}

auto ProceduralVoxel::pickPiece(glm::vec3 origin, glm::vec3 direction) -> VoxelPiece* {
	VoxelPiece* best = nullptr;
	float best_t = 1e6f;
	if (const voxel::Volume* shape = volume()) {
		if (const std::optional<voxel::VolumeHit> hit =
		        voxel::raycast(*shape, origin - glm::vec3(voxelOrigin()), direction, best_t)) {
			best_t = hit->t;
			best = pieceAt(hit->voxel + voxelOrigin());
		}
	}

	for (const PieceLayout& layout : m_layout) {
		if (layout.kind != PieceKind::carve || !layout.node.exists()) {
			continue;
		}
		auto* piece = reflect_cast<VoxelPiece>(&const_cast<Node&>(*layout.node));
		const VoxelPiece::GridRef grid = piece == nullptr ? nullptr : buildsAsync() ? piece->readyGrid() : piece->gridRef();
		if (grid == nullptr) {
			continue;
		}
		// Into piece voxels
		glm::vec3 local_origin;
		glm::vec3 local_direction;
		for (int axis = 0; axis < 3; ++axis) {
			const auto source = static_cast<size_t>(layout.placement.orientation.source[static_cast<size_t>(axis)]);
			const bool flip = layout.placement.orientation.flip[static_cast<size_t>(axis)];
			const float offset = static_cast<float>(layout.placement.offset[axis]);
			local_origin[static_cast<int>(source)] = flip ? offset - origin[axis] : origin[axis] - offset;
			local_direction[static_cast<int>(source)] = flip ? -direction[axis] : direction[axis];
		}
		const std::optional<voxel::VolumeHit> hit = voxel::raycast(*grid->volume, local_origin, local_direction, best_t);
		if (hit.has_value() && hit->t < best_t) {
			best_t = hit->t;
			best = piece;
		}
	}
	return best;
}

auto ProceduralVoxel::getPieceAt(glm::vec3 pos) -> Box<Node> {
	VoxelPiece* piece = pieceAt(glm::ivec3(glm::floor(pos)));
	return piece != nullptr ? piece->box() : Box<Node> {};
}

auto ProceduralVoxel::pieceAt(glm::ivec3 voxel) -> VoxelPiece* {
	// The last piece that draws that voxel is the one whose color shows there
	for (auto& it : std::views::reverse(m_layout)) {
		if (it.kind == PieceKind::carve || !it.node.exists()) {
			continue;
		}
		auto* piece = reflect_cast<VoxelPiece>(&const_cast<Node&>(*it.node));
		if (piece == nullptr) {
			continue;
		}
		// The editor never waits on a piece still drawing
		const VoxelPiece::GridRef grid = buildsAsync() ? piece->readyGrid() : piece->gridRef();
		if (grid == nullptr) {
			continue;
		}
		glm::ivec3 local;
		for (int axis = 0; axis < 3; ++axis) {
			const int32_t along = voxel[axis] - it.placement.offset[axis];
			local[it.placement.orientation.source[static_cast<size_t>(axis)]] =
			    it.placement.orientation.flip[static_cast<size_t>(axis)] ? -along - 1 : along;
		}
		if (grid->volume->containsVoxel(local) && grid->volume->isSolidAt(local)) {
			return piece;
		}
	}
	return nullptr;
}

void ProceduralVoxel::end() {
	if (renderer::VulkanRenderer::instance != nullptr) {
		renderer::VulkanRenderer::instance->setVoxelHighlight(this, nullptr);
	}
	m_highlight.reset();
	m_highlight_for = {};
	// The compose only reads its own snapshot so it is left to finish on its own
	m_compose_job = {};
	m_last_compose.reset();
	m_pending_projections = false;
}

void ProceduralVoxel::destroy() {
	end();
}

void ProceduralVoxel::sendLayout() {
	event::ProceduralVoxelLayout layout;
	layout.root_uid = uid();
	layout.pieces.reserve(m_layout.size());
	for (const PieceLayout& piece : m_layout) {
		auto* node = reflect_cast<VoxelPiece>(piece.node.exists() ? &const_cast<Node&>(*piece.node) : nullptr);
		if (node == nullptr) {
			continue;
		}
		event::VoxelPieceLayoutData data;
		data.uid = node->uid();
		data.name = std::string(node->name());
		data.kind = static_cast<uint32_t>(piece.kind);
		data.min = piece.bounds.min;
		data.max = piece.bounds.max;
		data.color = glm::vec3(piece.average_color);
		data.resizable = node->resizable();
		if (auto* volume = reflect_cast<VoxelVolume>(node)) {
			data.color_id = static_cast<uint32_t>(volume->getId());
		}
		for (const glm::vec4& plane : node->clipPlanes()) {
			data.planes.push_back(planeToShape(plane, piece.placement));
		}
		layout.pieces.push_back(std::move(data));
	}
	layout.palette_uid = UID(paletteUid());
	// The projections come from the last compose
	if (renderer::editorOverlays().projections && m_last_compose != nullptr && m_last_compose->has_projections) {
		layout.projections = m_last_compose->projections;
	}
	event::send<event::ProceduralVoxelLayout>(std::move(layout));
}

auto ProceduralVoxel::composePieces(std::span<VoxelPiece* const> only) -> std::optional<Composed> {
	std::vector<Collected> pieces;
	collect(*this, glm::mat4(1.0f), pieces);
	std::erase_if(pieces, [&](const Collected& entry) { return std::ranges::find(only, entry.piece) == only.end(); });
	const std::vector<Landing> landings = land(pieces, true);

	voxel::EditBounds bounds {};
	for (const Landing& landing : landings) {
		if (landing.kind == PieceKind::fill) {
			bounds = voxel::boundsUnion(bounds, landedBox(landing.piece->nominalSize(), landing.placement));
		}
	}
	if (bounds.empty()) {
		return std::nullopt;
	}

	// Brick aligned so the model lands on the same voxels it was built on
	const glm::ivec3 k_dim(static_cast<int32_t>(voxel::k_brick_dim));
	const glm::ivec3 origin = (bounds.min - glm::ivec3(glm::lessThan(bounds.min, glm::ivec3(0))) * (k_dim - 1)) / k_dim * k_dim;
	const glm::uvec3 bricks = glm::uvec3((bounds.max - origin + k_dim) / k_dim);
	Composed out {voxel::Volume(voxel::runtimeBrickPool(), bricks), origin};
	for (const Landing& landing : landings) {
		apply(out.volume, landing, origin);
	}
	return out;
}

void ProceduralVoxel::drawDebug() {
	VoxelNode::drawDebug();
	if (participatesIn(NodeOwnerParticipation::gameplay_tick)) {
		return;
	}
	const renderer::EditorOverlays& overlays = renderer::editorOverlays();
	const bool cutting = overlays.cut_active && overlays.cut_root == uid();
	const bool drawing_box = overlays.tool_box_active && overlays.tool_root == uid();

	const glm::mat4& world = getWorldTransform();
	const auto box = [&](glm::vec3 min, glm::vec3 max) {
		return world * glm::translate(glm::mat4(1.0f), (min + max) * 0.5f * voxel::k_voxel_size) *
		       glm::scale(glm::mat4(1.0f), (max - min) * voxel::k_voxel_size);
	};

	if (drawing_box) {
		// the outline shows the whole box even where it is hidden
		const glm::mat4 preview = box(glm::vec3(overlays.tool_box_min), glm::vec3(overlays.tool_box_max + 1));
		renderer::debugDrawShapeBox(preview, glm::vec4(glm::vec3(overlays.tool_box_color), 1.0f), false);
	}
	if (overlays.tool_box2_active && overlays.tool_root == uid()) {
		renderer::debugDrawShapeBox(
		    box(glm::vec3(overlays.tool_box2_min), glm::vec3(overlays.tool_box2_max + 1)),
		    glm::vec4(glm::vec3(overlays.tool_box_color), 1.0f),
		    false
		);
	}

	// The selected piece
	const glm::vec4 k_selection_orange {1.0f, 0.639f, 0.0f, 1.0f};
	for (const PieceLayout& piece : m_layout) {
		if (piece.node.exists() && piece.node->uid() == overlays.selected) {
			renderer::debugDrawShapeBox(box(glm::vec3(piece.bounds.min), glm::vec3(piece.bounds.max + 1)), k_selection_orange, false);
		}
	}
	if (!overlays.volume_edges && !cutting) {
		return;
	}

	voxel::EditBounds all {};
	for (const PieceLayout& piece : m_layout) {
		all = voxel::boundsUnion(all, piece.bounds);
		if (overlays.volume_edges) {
			const glm::vec4 color = piece.kind == PieceKind::carve ? glm::vec4(1.0f, 0.35f, 0.2f, 1.0f) : piece.average_color;
			renderer::debugDrawShapeBox(box(glm::vec3(piece.bounds.min), glm::vec3(piece.bounds.max + 1)), color, false);
		}
	}

	if (!cutting || all.empty()) {
		return;
	}

	const glm::vec3 normal = glm::vec3(overlays.cut_plane);
	const float length = glm::length(normal);
	if (length <= 1e-6f) {
		return;
	}
	const glm::vec3 n = normal / length;
	const glm::vec3 centre = (glm::vec3(all.min) + glm::vec3(all.max + 1)) * 0.5f;
	const glm::vec3 on_plane = centre - n * ((glm::dot(normal, centre) + overlays.cut_plane.w) / length);
	const float extent = glm::length(glm::vec3(all.max + 1 - all.min));

	const glm::vec3 helper = std::abs(n.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 u = glm::normalize(glm::cross(helper, n));
	const glm::vec3 v = glm::cross(n, u);
	const glm::mat4 basis(glm::vec4(u, 0.0f), glm::vec4(v, 0.0f), glm::vec4(n, 0.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

	const auto slab = [&](float from, float to, glm::vec4 color) {
		const glm::vec3 middle = (on_plane + n * ((from + to) * 0.5f)) * voxel::k_voxel_size;
		const glm::mat4 transform = world * glm::translate(glm::mat4(1.0f), middle) * basis *
		                            glm::scale(glm::mat4(1.0f), glm::vec3(extent, extent, std::abs(to - from)) * voxel::k_voxel_size);
		renderer::debugDrawShapeBox(transform, color, true);
	};
	slab(0.0f, extent * 0.5f, {1.0f, 0.15f, 0.15f, 0.12f});
	slab(-extent * 0.5f, 0.0f, {0.15f, 0.45f, 1.0f, 0.12f});
	slab(-0.05f, 0.05f, {1.0f, 1.0f, 1.0f, 0.5f});
}

}
