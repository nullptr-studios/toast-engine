#include "procedural_voxel.hpp"

#include "voxel_node_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <toast/assets/asset_manager.hpp>
#include <toast/log.hpp>
#include <toast/renderer/editor_overlays.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/voxel/assets/voxel_palette.hpp>
#include <toast/voxel/runtime_pool.hpp>
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

void ProceduralVoxel::rebuild() {
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

auto ProceduralVoxel::shapeKey() -> uint64_t {
	std::vector<Collected> pieces;
	collect(*this, glm::mat4(1.0f), pieces);

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
		if (overlays.tool_box_active && overlays.tool_root == uid()) {
			hashCombine(key, overlays.tool_box_kind);
			if (overlays.tool_piece != nullptr) {
				hashCombine(key, overlays.tool_piece->inputRevision());
			}
			const std::array<glm::vec3, 2> corners = {glm::vec3(overlays.tool_box_min), glm::vec3(overlays.tool_box_max)};
			hashFloats(key, &corners[0].x, 6);
		}
		hashCombine(key, overlays.selected.data());
		hashCombine(key, overlays.projections ? 1u : 0u);
	}
	return key;
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
	voxel::PaletteRemap built = voxel::buildRemap(*source, *target);
	if (!built.unmatched.empty()) {
		TOAST_WARN("Voxel", "'{}' skips {} color(s) of '{}' missing from this palette", name(), built.unmatched.size(), piece.name());
	}
	return built.table;
}

auto ProceduralVoxel::land(const std::vector<Collected>& pieces) -> std::vector<Landing> {
	std::vector<Landing> landings;
	landings.reserve(pieces.size());
	for (const Collected& entry : pieces) {
		const auto [placement, exact] = snappedPlacement(entry.to_root);
		entry.piece->setMisaligned(!exact);
		const voxel::Volume* grid = entry.piece->grid();
		if (grid == nullptr) {
			continue;
		}
		landings.push_back({entry.piece, grid, placement, remapFor(*entry.piece)});
	}
	return landings;
}

auto ProceduralVoxel::apply(voxel::Volume& target, const Landing& landing, glm::ivec3 origin) -> voxel::EditResult {
	voxel::LatticePlacement indexed = landing.placement;
	indexed.offset -= origin;
	switch (landing.piece->kind()) {
		case PieceKind::fill:
			return voxel::stampVolume(
			    target, *landing.grid, indexed, landing.remap, landing.piece->writeMode(), landing.piece->matchId()
			);
		case PieceKind::carve: return voxel::carveVolume(target, *landing.grid, indexed);
		case PieceKind::paint:
			return voxel::stampVolume(target, *landing.grid, indexed, landing.remap, voxel::WriteMode::solid_only, 0);
	}
	return {};
}

auto ProceduralVoxel::averageColor(const Landing& landing) -> glm::vec3 {
	const voxel::Palette& palette = resolvedPalette() != nullptr ? *resolvedPalette() : voxel::defaultPalette();
	const std::array<uint32_t, voxel::k_palette_size> counts = voxel::idHistogram(*landing.grid);
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

void ProceduralVoxel::buildShape() {
	ZoneScopedN("voxel::ComposeProcedural");    // NOLINT
	ZoneNameF("%.*s", static_cast<int>(name().size()), name().data());

	std::vector<Collected> pieces;
	collect(*this, glm::mat4(1.0f), pieces);
	const std::vector<Landing> landings = land(pieces);

	voxel::EditBounds grows {};
	for (const Landing& landing : landings) {
		if (landing.piece->kind() == PieceKind::fill) {
			grows = voxel::boundsUnion(grows, voxel::placedBounds(landing.grid->brickDims(), landing.placement));
		}
	}

	voxel::Volume* target = grows.empty() ? volume() : ensureContains(grows);
	m_layout.clear();
	m_layout.reserve(landings.size());
	for (const Landing& landing : landings) {
		m_layout.push_back({
		  .node = landing.piece->box(),
		  .kind = landing.piece->kind(),
		  .placement = landing.placement,
		  .bounds = landedBox(landing.piece->nominalSize(), landing.placement),
		  .average_color = glm::vec4(averageColor(landing), 1.0f),
		});
		if (target != nullptr) {
			commitEdit(apply(*target, landing, voxelOrigin()), "build");
		}
	}

	// Buckets paint the finished shape so they never depend on the order of the pieces
	if (target != nullptr) {
		std::vector<BucketFill> buckets;
		collectBuckets(*this, glm::mat4(1.0f), buckets);
		for (const BucketFill& fill : buckets) {
			commitEdit(voxel::floodFill(*target, toIndex(fill.seed), fill.bucket->colorId()), "bucket");
		}
	}

	if (!participatesIn(NodeOwnerParticipation::gameplay_tick)) {
		target = applyToolPreview(target);
		rebuildHighlight(target);
	}

	if (participatesIn(NodeOwnerParticipation::gameplay_tick)) {
		// In game the shape is only built once and destruction owns it after that
		if (!m_keep_grids) {
			for (const Landing& landing : landings) {
				landing.piece->releaseGrid();
			}
		}
	} else {
		sendLayout();
	}
}

auto ProceduralVoxel::applyToolPreview(voxel::Volume* target) -> voxel::Volume* {
	const renderer::EditorOverlays& overlays = renderer::editorOverlays();
	if (!overlays.tool_box_active || overlays.tool_root != uid()) {
		return target;
	}
	const voxel::EditBounds box {
	  glm::min(overlays.tool_box_min, overlays.tool_box_max), glm::max(overlays.tool_box_min, overlays.tool_box_max)
	};

	// The preview draws with the script the new volume will get, an outline only preview builds nothing
	const voxel::Volume* grid = overlays.tool_piece != nullptr ? overlays.tool_piece->grid() : nullptr;
	if (overlays.tool_box_kind > 2 || grid == nullptr) {
		return target;
	}

	// Fill grows the shape like a real volume would, carve and paint only touch what is there
	if (overlays.tool_box_kind == 0) {
		target = ensureContains(box);
	}
	if (target == nullptr) {
		return target;
	}
	const voxel::LatticePlacement placement {.offset = toIndex(box.min)};
	switch (overlays.tool_box_kind) {
		case 0:
			commitEdit(voxel::stampVolume(*target, *grid, placement, voxel::identityRemap(), voxel::WriteMode::replace, 0), "preview");
			break;
		case 1: commitEdit(voxel::carveVolume(*target, *grid, placement), "preview"); break;
		default:
			commitEdit(
			    voxel::stampVolume(*target, *grid, placement, voxel::identityRemap(), voxel::WriteMode::solid_only, 0), "preview"
			);
			break;
	}
	return target;
}

void ProceduralVoxel::rebuildHighlight(const voxel::Volume* target) {
	// The renderer reads the old volume until it is told about the new one
	if (renderer::VulkanRenderer::instance != nullptr) {
		renderer::VulkanRenderer::instance->setVoxelHighlight(this, nullptr);
	}
	m_highlight.reset();

	const toast::UID selected = renderer::editorOverlays().selected;
	if (target == nullptr || selected.data() == 0) {
		return;
	}
	const auto layout = std::ranges::find_if(m_layout, [&](const PieceLayout& piece) {
		return piece.node.exists() && piece.node->uid() == selected && piece.kind != PieceKind::carve;
	});
	auto* piece = layout != m_layout.end() ? reflect_cast<VoxelPiece>(&const_cast<Node&>(*layout->node)) : nullptr;
	const voxel::Volume* grid = piece != nullptr ? piece->grid() : nullptr;
	if (grid == nullptr) {
		return;
	}

	// The piece where the finished shape still has voxels, so later carves and pieces on top show through
	auto highlight = std::make_unique<voxel::Volume>(voxel::runtimeBrickPool(), target->brickDims());
	voxel::LatticePlacement indexed = layout->placement;
	indexed.offset -= voxelOrigin();
	voxel::stampVolume(*highlight, *grid, indexed, voxel::identityRemap(), voxel::WriteMode::replace, 0);
	const voxel::EditBounds covered {
	  glm::max(toIndex(layout->bounds.min), glm::ivec3(0)),
	  glm::min(toIndex(layout->bounds.max), glm::ivec3(target->voxelDims()) - 1)
	};
	for (int32_t z = covered.min.z; z <= covered.max.z; ++z) {
		for (int32_t y = covered.min.y; y <= covered.max.y; ++y) {
			for (int32_t x = covered.min.x; x <= covered.max.x; ++x) {
				const glm::ivec3 voxel {x, y, z};
				if (highlight->isSolidAt(voxel) && !target->isSolidAt(voxel)) {
					highlight->setVoxel(voxel, voxel::k_empty_palette_index);
				}
			}
		}
	}

	m_highlight = std::move(highlight);
	if (renderer::VulkanRenderer::instance != nullptr) {
		renderer::VulkanRenderer::instance->setVoxelHighlight(this, m_highlight.get());
	}
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
		const voxel::Volume* grid = piece != nullptr ? piece->grid() : nullptr;
		if (grid == nullptr) {
			continue;
		}
		glm::ivec3 local;
		for (int axis = 0; axis < 3; ++axis) {
			const int32_t along = voxel[axis] - it.placement.offset[axis];
			local[it.placement.orientation.source[static_cast<size_t>(axis)]] =
			    it.placement.orientation.flip[static_cast<size_t>(axis)] ? -along - 1 : along;
		}
		if (grid->containsVoxel(local) && grid->isSolidAt(local)) {
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
}

void ProceduralVoxel::destroy() {
	end();
}

namespace {

struct ProjectionAxes {
	int h;
	int v;
	int depth;
	int toward;
};

}

void ProceduralVoxel::addProjections(event::ProceduralVoxelLayout& layout) {
	ZoneScoped;
	const voxel::Volume* shape = volume();
	if (shape == nullptr) {
		return;
	}
	const voxel::Palette& palette = resolvedPalette() != nullptr ? *resolvedPalette() : voxel::defaultPalette();
	const glm::ivec3 dims = glm::ivec3(shape->voxelDims());
	const glm::ivec3 origin = voxelOrigin();

	constexpr std::array<ProjectionAxes, 3> views {
	  ProjectionAxes {0, 2, 1,  1},
     ProjectionAxes {1, 2, 0, -1},
     ProjectionAxes {0, 1, 2, -1}
	};
	for (const ProjectionAxes& view : views) {
		event::VoxelProjectionData projection;
		projection.min_h = origin[view.h];
		projection.min_v = origin[view.v];
		projection.width = static_cast<uint32_t>(dims[view.h]);
		projection.height = static_cast<uint32_t>(dims[view.v]);
		projection.colors.assign(static_cast<size_t>(projection.width) * projection.height * 4, 0);
		projection.depths.assign(static_cast<size_t>(projection.width) * projection.height, std::numeric_limits<int16_t>::max());
		projection.edges_h.assign(static_cast<size_t>(projection.width + 1) * projection.height * 4, 0);
		projection.edges_v.assign(static_cast<size_t>(projection.width) * (projection.height + 1) * 4, 0);
		layout.projections.push_back(std::move(projection));
	}

	for (uint32_t index = 0; index < shape->brickCount(); ++index) {
		const glm::ivec3 brick = shape->brickAtIndex(index);
		const voxel::BrickEntry entry = shape->entryAt(brick);
		if (entry.tag() == voxel::BrickTag::empty) {
			continue;
		}
		const bool uniform = entry.tag() == voxel::BrickTag::uniform;
		const uint8_t* materials = uniform ? nullptr : std::as_const(*shape->pool()).material(entry.payload()).data();
		for (uint32_t i = 0; i < voxel::k_brick_voxel_count; ++i) {
			const uint8_t id = uniform ? static_cast<uint8_t>(entry.payload()) : materials[i];
			if (id == voxel::k_empty_palette_index) {
				continue;
			}
			const voxel::BrickCoord local = voxel::localFromIndex(i);
			const glm::ivec3 cell = brick * static_cast<int32_t>(voxel::k_brick_dim) + glm::ivec3(local.x, local.y, local.z);
			for (size_t v = 0; v < views.size(); ++v) {
				event::VoxelProjectionData& projection = layout.projections[v];
				const size_t slot = static_cast<size_t>(cell[views[v].h]) + (static_cast<size_t>(cell[views[v].v]) * projection.width);
				const voxel::PaletteEntry& color = palette.entries[id];

				// See through
				const auto mark = [&](std::vector<uint8_t>& edges, size_t at) {
					edges[(at * 4) + 0] = color.albedo_r;
					edges[(at * 4) + 1] = color.albedo_g;
					edges[(at * 4) + 2] = color.albedo_b;
					edges[(at * 4) + 3] = 255;
				};
				const auto h = static_cast<size_t>(cell[views[v].h]);
				const auto row = static_cast<size_t>(cell[views[v].v]);
				const auto empty = [&](int axis, int step) {
					glm::ivec3 next = cell;
					next[axis] += step;
					return !shape->containsVoxel(next) || !shape->isSolidAt(next);
				};
				if (empty(views[v].h, -1)) {
					mark(projection.edges_h, h + (row * (projection.width + 1)));
				}
				if (empty(views[v].h, 1)) {
					mark(projection.edges_h, h + 1 + (row * (projection.width + 1)));
				}
				if (empty(views[v].v, -1)) {
					mark(projection.edges_v, h + (row * projection.width));
				}
				if (empty(views[v].v, 1)) {
					mark(projection.edges_v, h + ((row + 1) * projection.width));
				}

				const auto depth = static_cast<int16_t>(views[v].toward * (cell[views[v].depth] + origin[views[v].depth]));
				projection.depths[slot] = std::min(projection.depths[slot], depth);
			}
		}
	}

	// See through fill: every run of one color along the depth is a layer at 0.1 alpha, blended far to near
	constexpr float k_layer_alpha = 0.1f;
	for (size_t v = 0; v < views.size(); ++v) {
		event::VoxelProjectionData& projection = layout.projections[v];
		const ProjectionAxes& view = views[v];
		const int length = dims[view.depth];
		for (uint32_t row = 0; row < projection.height; ++row) {
			for (uint32_t h = 0; h < projection.width; ++h) {
				glm::vec3 color(0.0f);
				float alpha = 0.0f;
				uint8_t previous = voxel::k_empty_palette_index;
				for (int step = 0; step < length; ++step) {
					glm::ivec3 cell;
					cell[view.h] = static_cast<int>(h);
					cell[view.v] = static_cast<int>(row);
					cell[view.depth] = view.toward > 0 ? length - 1 - step : step;
					const uint8_t id = shape->materialAt(cell);
					if (id != previous && id != voxel::k_empty_palette_index) {
						const voxel::PaletteEntry& entry = palette.entries[id];
						const glm::vec3 layer = glm::vec3(entry.albedo_r, entry.albedo_g, entry.albedo_b) / 255.0f;
						color = layer * k_layer_alpha + color * (1.0f - k_layer_alpha);
						alpha = k_layer_alpha + (alpha * (1.0f - k_layer_alpha));
					}
					previous = id;
				}
				if (alpha <= 0.0f) {
					continue;
				}
				// color is premultiplied, stored straight
				const size_t slot = static_cast<size_t>(h) + (static_cast<size_t>(row) * projection.width);
				for (int c = 0; c < 3; ++c) {
					projection.colors[(slot * 4) + c] =
					    static_cast<uint8_t>(std::lround(glm::clamp(color[c] / alpha, 0.0f, 1.0f) * 255.0f));
				}
				projection.colors[(slot * 4) + 3] = static_cast<uint8_t>(std::lround(alpha * 255.0f));
			}
		}
	}
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
	if (renderer::editorOverlays().projections) {
		addProjections(layout);
	}
	event::send<event::ProceduralVoxelLayout>(layout);
}

auto ProceduralVoxel::composePieces(std::span<VoxelPiece* const> only) -> std::optional<Composed> {
	std::vector<Collected> pieces;
	collect(*this, glm::mat4(1.0f), pieces);
	std::erase_if(pieces, [&](const Collected& entry) { return std::ranges::find(only, entry.piece) == only.end(); });
	const std::vector<Landing> landings = land(pieces);

	voxel::EditBounds bounds {};
	for (const Landing& landing : landings) {
		if (landing.piece->kind() == PieceKind::fill) {
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
