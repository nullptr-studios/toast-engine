#include "voxel_node.hpp"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/physics/contact_events.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/voxel/assets/voxel_model.hpp>
#include <toast/voxel/assets/voxel_palette.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace toast {

namespace {

/// A UID shared by two assets resolves to the wrong one so check through the untyped base
template<typename T>
[[nodiscard]]
auto voxelNodeAssetOfType(const assets::Handle<T>& handle, std::string_view type) -> const T* {
	if (!handle.hasValue()) {
		return nullptr;
	}
	const assets::Asset& asset = static_cast<const assets::HandleBase&>(handle).get();
	return asset.type() == type ? static_cast<const T*>(&asset) : nullptr;
}

}

auto VoxelNode::paletteUid() const -> uint64_t {
	if (m_palette.uid().data() != 0) {
		return m_palette.uid().data();
	}
	if (const auto* model = voxelNodeAssetOfType(m_model, "voxel_model")) {
		return model->paletteUid();
	}
	return 0;
}

auto VoxelNode::getModel() const -> const assets::Handle<assets::VoxelModel>& {
	return m_model;
}

auto VoxelNode::getModel() -> assets::Handle<assets::VoxelModel>& {
	return m_model;
}

void VoxelNode::setModel(assets::Handle<assets::VoxelModel> model) {
	if (m_model == model) {
		return;
	}
	m_volume_stale = true;
	m_model = std::move(model);
	m_model_palette = {};
	++m_revision;
	refreshVolume();
}

auto VoxelNode::getPalette() const -> const assets::Handle<assets::VoxelPalette>& {
	return m_palette;
}

void VoxelNode::setPalette(assets::Handle<assets::VoxelPalette> palette) {
	if (m_palette == palette) {
		return;
	}
	m_palette = std::move(palette);
	++m_revision;
}

auto VoxelNode::resolvedModel() const -> const assets::VoxelModel* {
	return voxelNodeAssetOfType(m_model, "voxel_model");
}

auto VoxelNode::localBoundingSphere() const -> glm::vec4 {
	glm::vec3 extent;
	if (m_volume != nullptr) {
		extent = glm::vec3(m_volume->brickDims()) * voxel::k_brick_size;
	} else if (const auto* model = voxelNodeAssetOfType(m_model, "voxel_model")) {
		extent = glm::vec3(model->brickDims()) * voxel::k_brick_size;
	} else {
		return glm::vec4(0.0f);
	}
	const glm::vec3 corner = glm::vec3(m_voxel_origin) * voxel::k_voxel_size;
	return {corner + (extent * 0.5f), glm::length(extent) * 0.5f};
}

auto VoxelNode::latticePlacement() const -> std::optional<voxel::LatticePlacement> {
	std::optional<voxel::LatticePlacement> placement = voxel::placementFromTransform(getWorldTransform());
	if (!placement.has_value()) {
		return placement;
	}
	for (int axis = 0; axis < 3; ++axis) {
		const int32_t along = m_voxel_origin[placement->orientation.source[axis]];
		placement->offset[axis] += placement->orientation.flip[axis] ? -along : along;
	}
	return placement;
}

auto VoxelNode::volumeLocalTransform() const -> glm::mat4 {
	return glm::translate(glm::mat4(1.0f), glm::vec3(m_voxel_origin) * voxel::k_voxel_size);
}

auto VoxelNode::volume() -> voxel::Volume* {
	const assets::VoxelModel* model = voxelNodeAssetOfType(m_model, "voxel_model");

	if (m_model.hasValue() && model == nullptr && m_reported_wrong_model != m_model.uid().data()) {
		m_reported_wrong_model = m_model.uid().data();
		TOAST_WARN(
		    "Voxel",
		    "'{}' names model {}, which resolves to a '{}' asset, not a voxel model - two assets share that UID. Reimport the "
		    "source to give them separate ones",
		    name(),
		    m_model.uid().get(),
		    static_cast<const assets::HandleBase&>(m_model).get().type()
		);
	}

	if (model != m_instanced_from || m_volume_stale) {
		retireVolume();
		m_instanced_from = model;
		m_volume_stale = false;
		m_model_palette = {};
		m_voxel_origin = glm::ivec3(0);
		m_edit_shape_pending = true;

		if (model != nullptr) {
			std::optional<voxel::Volume> instance = model->instantiate(voxel::runtimeBrickPool());
			if (instance.has_value()) {
				m_volume = std::make_unique<voxel::Volume>(std::move(*instance));
			} else {
				TOAST_WARN(
				    "Voxel",
				    "'{}' could not be instantiated: the runtime brick pool is out of its {} bricks",
				    name(),
				    voxel::k_runtime_brick_capacity
				);
			}
		}
		++m_revision;
	}
	return m_volume.get();
}

auto VoxelNode::resolvedPalette() -> const voxel::Palette* {
	if (const auto* palette = voxelNodeAssetOfType(m_palette, "voxel_palette")) {
		return &palette->palette();
	}

	if (m_palette.hasValue() && m_reported_wrong_palette != m_palette.uid().data()) {
		m_reported_wrong_palette = m_palette.uid().data();
		TOAST_WARN(
		    "Voxel",
		    "'{}' names palette {}, which resolves to a '{}' asset, not a voxel palette - two assets share that UID",
		    name(),
		    m_palette.uid().get(),
		    static_cast<const assets::HandleBase&>(m_palette).get().type()
		);
	}

	if (m_palette.uid().data() != 0) {
		return nullptr;
	}

	const auto* model = voxelNodeAssetOfType(m_model, "voxel_model");
	if (model == nullptr || model->paletteUid() == 0) {
		return nullptr;
	}
	if (m_model_palette.uid().data() != model->paletteUid()) {
		m_model_palette = assets::load<assets::VoxelPalette>(UID(model->paletteUid()));
	}
	const auto* palette = voxelNodeAssetOfType(m_model_palette, "voxel_palette");
	return palette != nullptr ? &palette->palette() : nullptr;
}

auto VoxelNode::resolvedMaterialLibrary() -> const voxel::MaterialLibrary* {
	const assets::VoxelPalette* palette = voxelNodeAssetOfType(m_palette, "voxel_palette");
	if (palette == nullptr && m_palette.uid().data() == 0) {
		const assets::VoxelModel* model = resolvedModel();
		if (model != nullptr && model->paletteUid() != 0) {
			if (m_model_palette.uid().data() != model->paletteUid()) {
				m_model_palette = assets::load<assets::VoxelPalette>(UID(model->paletteUid()));
			}
			palette = voxelNodeAssetOfType(m_model_palette, "voxel_palette");
		}
	}
	if (palette == nullptr) {
		return nullptr;
	}

	return &palette->materialLibrary();
}

void VoxelNode::releaseVolume() {
	m_volume.reset();
	m_retired_volumes.clear();
	m_instanced_from = nullptr;
	m_voxel_origin = glm::ivec3(0);
	m_volume_stale = true;
}

void VoxelNode::retireVolume() {
	if (m_volume != nullptr && physicsBound()) {
		m_retired_volumes.push_back(std::move(m_volume));
	}
	m_volume.reset();
}

void VoxelNode::refreshVolume() {
	ZoneScopedN("voxel::RefreshVolume");    // NOLINT
	if (m_rebuild_requested) {
		m_rebuild_requested = false;
		m_volume_stale = true;
	}
	volume();
	if (!m_edit_shape_pending) {
		return;
	}
	m_edit_shape_pending = false;
	m_pending_events = {};
	m_building_shape = true;
	buildShape();
	m_building_shape = false;
	if (!buildsAsync()) {
		finishShape();
	}
}

void VoxelNode::finishShape() {
	m_building_shape = true;
	call("editShape");
	m_building_shape = false;
	rebuilt_shape.fire();
}

namespace {

constexpr int32_t k_max_bricks_per_axis = 512;
constexpr int64_t k_max_bricks = int64_t {1} << 21;

[[nodiscard]]
auto tooBig(glm::ivec3 dims) noexcept -> bool {
	return glm::any(glm::greaterThan(dims, glm::ivec3(k_max_bricks_per_axis))) ||
	       static_cast<int64_t>(dims.x) * dims.y * dims.z > k_max_bricks;
}

}

auto VoxelNode::replaceVolume(const voxel::EditBounds& bounds) -> voxel::Volume* {
	if (bounds.empty()) {
		return nullptr;
	}
	const glm::ivec3 first {bounds.min.x >> 3, bounds.min.y >> 3, bounds.min.z >> 3};
	const glm::ivec3 last {bounds.max.x >> 3, bounds.max.y >> 3, bounds.max.z >> 3};
	const glm::ivec3 dims = last - first + 1;
	if (tooBig(dims)) {
		return nullptr;
	}
	retireVolume();
	m_volume = std::make_unique<voxel::Volume>(voxel::runtimeBrickPool(), glm::uvec3(dims));
	m_voxel_origin = first * static_cast<int32_t>(voxel::k_brick_dim);
	++m_revision;
	return m_volume.get();
}

auto VoxelNode::ensureContains(const voxel::EditBounds& bounds) -> voxel::Volume* {
	constexpr int32_t max_bricks_per_axis = 512;
	constexpr int64_t max_bricks = int64_t {1} << 21;

	voxel::Volume* current = volume();
	if (bounds.empty()) {
		return current;
	}

	const glm::ivec3 want_min {bounds.min.x >> 3, bounds.min.y >> 3, bounds.min.z >> 3};
	const glm::ivec3 want_max {bounds.max.x >> 3, bounds.max.y >> 3, bounds.max.z >> 3};

	glm::ivec3 new_min = want_min;
	glm::ivec3 new_max = want_max;
	const glm::ivec3 current_min = m_voxel_origin / static_cast<int32_t>(voxel::k_brick_dim);
	if (current != nullptr) {
		const glm::ivec3 current_max = current_min + glm::ivec3(current->brickDims()) - 1;
		if (glm::all(glm::greaterThanEqual(want_min, current_min)) && glm::all(glm::lessThanEqual(want_max, current_max))) {
			return current;
		}
		new_min = glm::min(new_min, current_min);
		new_max = glm::max(new_max, current_max);
	}

	const glm::ivec3 dims = new_max - new_min + 1;
	if (glm::any(glm::greaterThan(dims, glm::ivec3(max_bricks_per_axis))) ||
	    static_cast<int64_t>(dims.x) * dims.y * dims.z > max_bricks) {
		TOAST_WARN(
		    "Voxel", "'{}' cannot grow to {}x{}x{} bricks, the edit is clipped to the current volume", name(), dims.x, dims.y, dims.z
		);
		return current;
	}

	if (current != nullptr) {
		voxel::Volume grown = voxel::Volume::adoptResized(*current, current_min - new_min, glm::uvec3(dims));
		retireVolume();
		m_volume = std::make_unique<voxel::Volume>(std::move(grown));
	} else {
		m_volume = std::make_unique<voxel::Volume>(voxel::runtimeBrickPool(), glm::uvec3(dims));
	}
	m_voxel_origin = new_min * static_cast<int32_t>(voxel::k_brick_dim);
	++m_revision;
	return m_volume.get();
}

void VoxelNode::commitEdit(const voxel::EditResult& result, std::string_view operation) {
	if (result.pool_exhausted) {
		TOAST_WARN(
		    "Voxel",
		    "'{}' {} stopped early: the runtime brick pool is out of its {} bricks",
		    name(),
		    operation,
		    voxel::k_runtime_brick_capacity
		);
	}
	if (result.changed == 0) {
		return;
	}
	++m_revision;
	m_split_pending = true;

	if (!m_building_shape && m_volume != nullptr && m_volume->solidVoxelCount() == 0) {
		m_pending_events.emptied = true;
	}
}

void VoxelNode::recordDamage(uint32_t count, glm::vec3 index_sum) {
	m_pending_events.damaged_voxels += count;
	m_pending_events.damaged_sum += index_sum + (glm::vec3(m_voxel_origin) + 0.5f) * static_cast<float>(count);
}

void VoxelNode::postPhysics() {
	if (m_pending_events.damaged_voxels == 0 && m_pending_events.broken_pieces.empty() && !m_pending_events.emptied) {
		return;
	}
	const PendingEvents events = std::exchange(m_pending_events, {});

	if (events.damaged_voxels > 0) {
		const glm::vec3 centre = events.damaged_sum / static_cast<float>(events.damaged_voxels);
		damaged.fire(static_cast<int>(events.damaged_voxels), voxelToWorld(centre));
	}
	for (const int voxels : events.broken_pieces) {
		broke_apart.fire(voxels);
	}
	if (events.emptied) {
		emptied.fire();
	}
}

template<typename Kernel>
void VoxelNode::edit(const voxel::EditBounds& local_bounds, bool grows, std::string_view operation, Kernel&& kernel) {
	voxel::Volume* target = grows ? ensureContains(local_bounds) : volume();
	if (target == nullptr) {
		return;
	}
	commitEdit(kernel(*target), operation);
}

using _detail::toId;
using _detail::toVoxel;

auto VoxelNode::getVoxel(glm::vec3 pos) -> int {
	const voxel::Volume* current = volume();
	return current != nullptr ? current->materialAt(toIndex(toVoxel(pos))) : 0;
}

void VoxelNode::setVoxel(glm::vec3 pos, int id) {
	const glm::ivec3 cell = toVoxel(pos);
	const uint8_t material = toId(id);
	edit({cell, cell}, material != voxel::k_empty_palette_index, "setVoxel", [&](voxel::Volume& target) {
		voxel::EditResult result;
		const glm::ivec3 index = toIndex(cell);
		const voxel::Volume::VoxelWrite write = target.setVoxel(index, material);
		result.changed = write.changed ? 1 : 0;
		result.pool_exhausted = !write.changed && target.containsVoxel(index) && write.previous_material != material;
		return result;
	});
}

void VoxelNode::removeVoxel(glm::vec3 pos) {
	setVoxel(pos, voxel::k_empty_palette_index);
}

void VoxelNode::fillBox(glm::vec3 min, glm::vec3 max, int id, voxel::WriteMode mode, int match_id) {
	const voxel::EditBounds bounds = voxel::boxBounds(toVoxel(min), toVoxel(max));
	const voxel::WriteBrush brush {.id = toId(id), .mode = mode, .match_id = toId(match_id)};
	edit(bounds, voxel::canAdd(brush.id, brush.mode, brush.match_id), "fillBox", [&](voxel::Volume& target) {
		return voxel::fillBox(target, toIndex(bounds.min), toIndex(bounds.max), brush);
	});
}

void VoxelNode::fillSphere(glm::vec3 center, float radius, int id, voxel::WriteMode mode, int match_id) {
	const voxel::WriteBrush brush {.id = toId(id), .mode = mode, .match_id = toId(match_id)};
	edit(
	    voxel::sphereBounds(center, radius),
	    voxel::canAdd(brush.id, brush.mode, brush.match_id),
	    "fillSphere",
	    [&](voxel::Volume& target) { return voxel::fillSphere(target, toIndex(center), radius, brush); }
	);
}

void VoxelNode::fillCylinder(glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode, int match_id) {
	const voxel::WriteBrush brush {.id = toId(id), .mode = mode, .match_id = toId(match_id)};
	edit(
	    voxel::segmentBounds(a, b, radius),
	    voxel::canAdd(brush.id, brush.mode, brush.match_id),
	    "fillCylinder",
	    [&](voxel::Volume& target) { return voxel::fillCylinder(target, toIndex(a), toIndex(b), radius, brush); }
	);
}

void VoxelNode::fillLine(glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode, int match_id) {
	const voxel::WriteBrush brush {.id = toId(id), .mode = mode, .match_id = toId(match_id)};
	edit(
	    voxel::segmentBounds(a, b, radius),
	    voxel::canAdd(brush.id, brush.mode, brush.match_id),
	    "fillLine",
	    [&](voxel::Volume& target) { return voxel::fillCapsule(target, toIndex(a), toIndex(b), radius, brush); }
	);
}

void VoxelNode::carveBox(glm::vec3 min, glm::vec3 max) {
	fillBox(min, max, voxel::k_empty_palette_index);
}

void VoxelNode::carveSphere(glm::vec3 center, float radius) {
	fillSphere(center, radius, voxel::k_empty_palette_index);
}

void VoxelNode::carveCylinder(glm::vec3 a, glm::vec3 b, float radius) {
	fillCylinder(a, b, radius, voxel::k_empty_palette_index);
}

void VoxelNode::carveLine(glm::vec3 a, glm::vec3 b, float radius) {
	fillLine(a, b, radius, voxel::k_empty_palette_index);
}

void VoxelNode::paintBox(glm::vec3 min, glm::vec3 max, int id) {
	if (toId(id) != voxel::k_empty_palette_index) {
		fillBox(min, max, id, voxel::WriteMode::solid_only);
	}
}

void VoxelNode::paintSphere(glm::vec3 center, float radius, int id) {
	if (toId(id) != voxel::k_empty_palette_index) {
		fillSphere(center, radius, id, voxel::WriteMode::solid_only);
	}
}

void VoxelNode::replaceId(int from, int to) {
	edit({}, false, "replaceId", [&](voxel::Volume& target) { return voxel::replaceId(target, toId(from), toId(to)); });
}

void VoxelNode::slice(glm::vec3 point, glm::vec3 normal, int side) {
	edit({}, false, "slice", [&](voxel::Volume& target) { return voxel::slice(target, toIndex(point), normal, side); });
}

void VoxelNode::extrudeFace(glm::vec3 pos, glm::vec3 normal, int distance) {
	const voxel::Volume* current = volume();
	const glm::ivec3 axis = voxel::dominantAxis(normal);
	if (current == nullptr || distance == 0 || axis == glm::ivec3(0)) {
		return;
	}

	std::vector<glm::ivec3> face = voxel::collectFace(*current, toIndex(toVoxel(pos)), axis);
	if (face.empty()) {
		return;
	}
	for (glm::ivec3& cell : face) {
		cell += m_voxel_origin;
	}
	voxel::EditBounds bounds {face.front(), face.front()};
	for (const glm::ivec3& cell : face) {
		bounds.min = glm::min(bounds.min, cell);
		bounds.max = glm::max(bounds.max, cell);
	}
	if (distance > 0) {
		bounds = voxel::boundsUnion(bounds, {bounds.min + (axis * distance), bounds.max + (axis * distance)});
	}

	edit(bounds, distance > 0, "extrudeFace", [&](voxel::Volume& target) {
		for (glm::ivec3& cell : face) {
			cell = toIndex(cell);
		}
		return voxel::extrudeFace(target, face, axis, distance);
	});
}

void VoxelNode::stamp(
    const assets::Handle<assets::VoxelModel>& asset, glm::vec3 pos, glm::quat rotation, voxel::WriteMode mode, int match_id
) {
	const assets::VoxelModel* model = voxelNodeAssetOfType(asset, "voxel_model");
	if (model == nullptr) {
		TOAST_WARN("Voxel", "'{}' stamp needs a voxel model asset", name());
		return;
	}
	std::optional<voxel::Volume> piece = model->instantiate(voxel::runtimeBrickPool());
	if (!piece.has_value()) {
		return;
	}

	voxel::PaletteRemapTable remap = voxel::identityRemap();
	const voxel::Palette* target_palette = resolvedPalette();
	if (target_palette != nullptr && model->paletteUid() != 0 && model->paletteUid() != paletteUid()) {
		const assets::Handle<assets::VoxelPalette> piece_handle = assets::load<assets::VoxelPalette>(UID(model->paletteUid()));
		if (const auto* piece_palette = voxelNodeAssetOfType(piece_handle, "voxel_palette")) {
			voxel::PaletteRemap built = voxel::buildRemap(piece_palette->palette(), *target_palette);
			if (!built.unmatched.empty()) {
				TOAST_WARN(
				    "Voxel",
				    "'{}' stamp skips {} colour(s) of model {} missing from this node's palette",
				    name(),
				    built.unmatched.size(),
				    asset.uid().get()
				);
			}
			remap = built.table;
		}
	}

	voxel::LatticePlacement placement {
	  .orientation = voxel::snapOrientation(glm::mat3_cast(glm::normalize(rotation))), .offset = toVoxel(pos)
	};
	const voxel::EditBounds bounds = voxel::placedBounds(piece->brickDims(), placement);
	edit(bounds, voxel::canAdd(1, mode, toId(match_id)), "stamp", [&](voxel::Volume& target) {
		voxel::LatticePlacement indexed = placement;
		indexed.offset = toIndex(placement.offset);
		return voxel::stampVolume(target, *piece, indexed, remap, mode, toId(match_id));
	});
}

auto VoxelNode::copy(glm::vec3 min, glm::vec3 max) -> int {
	const voxel::EditBounds bounds = voxel::boxBounds(toVoxel(min), toVoxel(max));
	voxel::Region region;
	if (const voxel::Volume* current = volume()) {
		region = voxel::copyRegion(*current, toIndex(bounds.min), toIndex(bounds.max));
	} else {
		region.size = bounds.max - bounds.min + 1;
		region.ids.assign(static_cast<size_t>(region.size.x) * region.size.y * region.size.z, voxel::k_empty_palette_index);
	}

	const auto free_slot =
	    std::ranges::find_if(m_regions, [](const std::optional<voxel::Region>& slot) { return !slot.has_value(); });
	if (free_slot != m_regions.end()) {
		*free_slot = std::move(region);
		return static_cast<int>(free_slot - m_regions.begin()) + 1;
	}
	m_regions.emplace_back(std::move(region));
	return static_cast<int>(m_regions.size());
}

void VoxelNode::paste(int region, glm::vec3 pos, voxel::WriteMode mode, int match_id) {
	if (region <= 0 || std::cmp_greater(region, m_regions.size()) || !m_regions[region - 1].has_value()) {
		TOAST_WARN("Voxel", "'{}' paste was given region {}, which copy never returned or clearRegions freed", name(), region);
		return;
	}
	const voxel::Region& source = *m_regions[region - 1];
	const glm::ivec3 at = toVoxel(pos);
	edit({at, at + source.size - 1}, voxel::canAdd(1, mode, toId(match_id)), "paste", [&](voxel::Volume& target) {
		return voxel::pasteRegion(target, source, toIndex(at), mode, toId(match_id));
	});
}

void VoxelNode::clearRegions() {
	m_regions.clear();
}

auto VoxelNode::getSize() -> glm::vec3 {
	const voxel::Volume* current = volume();
	return current != nullptr ? glm::vec3(current->voxelDims()) : glm::vec3(0.0f);
}

auto VoxelNode::getBounds() -> std::vector<glm::vec3> {
	const voxel::Volume* current = volume();
	if (current == nullptr) {
		return {};
	}
	const std::optional<voxel::EditBounds> bounds = voxel::occupiedBounds(*current);
	if (!bounds.has_value()) {
		return {};
	}
	return {glm::vec3(bounds->min + m_voxel_origin), glm::vec3(bounds->max + m_voxel_origin)};
}

auto VoxelNode::getVoxelCount() -> int {
	const voxel::Volume* current = volume();
	return current != nullptr ? static_cast<int>(current->solidVoxelCount()) : 0;
}

auto VoxelNode::worldToVoxel(glm::vec3 pos) -> glm::vec3 {
	syncTransform();
	return glm::vec3(glm::inverse(getWorldTransform()) * glm::vec4(pos, 1.0f)) / voxel::k_voxel_size;
}

auto VoxelNode::voxelToWorld(glm::vec3 pos) -> glm::vec3 {
	syncTransform();
	return {getWorldTransform() * glm::vec4(pos * voxel::k_voxel_size, 1.0f)};
}

void VoxelNode::sleep() {
	physics::Simulator::sleepBody(bodyID());
}

void VoxelNode::wake() {
	physics::Simulator::wakeBody(bodyID());
}

void VoxelNode::publishPhysicsState(
    bool is_awake, const glm::vec3& current_linear_velocity, const glm::vec3& current_angular_velocity
) {
	const bool state_changed = awake != is_awake;
	awake = is_awake;
	linear_velocity = current_linear_velocity;
	angular_velocity = current_angular_velocity;

	if (not state_changed) {
		return;
	}

	if (awake) {
		woke_up.fire();
	} else {
		went_to_sleep.fire();
	}
}

void VoxelNode::applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation) {
	world_position = position;
	world_rotation = rotation;
	syncTransform();
}

void VoxelNode::onEditorTransformChanged() {
	syncTransform();
	physics::Simulator::setBodyTransform(bodyID(), world_position, world_rotation);
}

void VoxelNode::handleContactBegin(const physics::BroadPhasePair& pair) {
	physics::BodyID other_body;
	if (pair.a.body == m_body) {
		other_body = pair.b.body;
	} else if (pair.b.body == m_body) {
		other_body = pair.a.body;
	} else {
		return;
	}

	const auto active = std::ranges::find(m_active_contacts, other_body, &ActiveContact::other_body);
	if (active != m_active_contacts.end()) {
		++active->shape_pair_count;
		return;
	}

	const toast::Box<toast::Node> other_node = physics::Simulator::nodeFor(other_body);
	m_active_contacts.emplace_back(ActiveContact {.other_body = other_body, .other_node = other_node, .shape_pair_count = 1});
	contact_begin.fire(other_node);
}

void VoxelNode::handleContactEnd(const physics::BroadPhasePair& pair) {
	physics::BodyID other_body;
	if (pair.a.body == m_body) {
		other_body = pair.b.body;
	} else if (pair.b.body == m_body) {
		other_body = pair.a.body;
	} else {
		return;
	}

	const auto active = std::ranges::find(m_active_contacts, other_body, &ActiveContact::other_body);
	if (active == m_active_contacts.end()) {
		return;
	}

	if (active->shape_pair_count > 1) {
		--active->shape_pair_count;
		return;
	}

	const toast::Box<toast::Node> other_node = active->other_node;
	m_active_contacts.erase(active);
	contact_end.fire(other_node);
}

void VoxelNode::updateInspectorMessages() {
	static const toast::NodeMessage model_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 1,
	  .text = "Voxel node has no valid model",
	};
	static const toast::NodeMessage palette_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 1,
	  .text = "Voxel node has no valid palette",
	};
	static const toast::NodeMessage material_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 1,
	  .text = "Voxel node has no valid physical material library",
	};
	if (resolvedModel() != nullptr) {
		removeInspectorMessage(model_message);
	} else {
		addInspectorMessage(model_message);
	}
	if (resolvedPalette() != nullptr) {
		removeInspectorMessage(palette_message);
	} else {
		addInspectorMessage(palette_message);
	}
	if (resolvedMaterialLibrary() != nullptr) {
		removeInspectorMessage(material_message);
	} else {
		addInspectorMessage(material_message);
	}
}

void VoxelNode::onReflectedFieldChanged(std::string_view field_name) {
	Node3D::onReflectedFieldChanged(field_name);
	if (field_name == "m_model") {
		m_volume_stale = true;
		++m_revision;
		refreshVolume();
	}
}

void VoxelNode::onScriptsReloaded() {
	m_rebuild_requested = true;
}

void VoxelNode::init() {
	// you dont need a model any more
	m_volume_stale = true;
	m_shape_key = shapeKey();
	refreshVolume();

	m_registered_proxy = renderer::registerVoxelNodeProxy(this);
	m_debug_visible = enabled();
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->registerDebugDraw(this, [](toast::Node3D& node) {
			static_cast<VoxelNode&>(node).drawDebug();
		});
	}
}

void VoxelNode::editorTick() {
	if (buildsAsync()) {
		tickAsyncBuild();
		return;
	}
	if (const uint64_t key = shapeKey(); key != m_shape_key) {
		m_shape_key = key;
		m_rebuild_requested = true;
	}
	refreshVolume();
}

void VoxelNode::earlyTick() {
	refreshVolume();
}

void VoxelNode::begin() {
	refreshVolume();

	if (not render_only && not m_registration_requested && participatesIn(NodeOwnerParticipation::gameplay_tick)) {
		m_registration_requested = true;
		physics::Simulator::registerVoxelNode(*this);
		listener().subscribe<event::ContactBegin>("voxel_contact_begin", [this](const event::ContactBegin& contact) {
			handleContactBegin(contact.contact.pair);
		});
		listener().subscribe<event::ContactEnd>("voxel_contact_end", [this](const event::ContactEnd& contact) {
			handleContactEnd(contact.pair);
		});
	}
}

void VoxelNode::end() {
	if (m_registration_requested) {
		m_registration_requested = false;
		listener().unsubscribe<event::ContactBegin>("voxel_contact_begin");
		listener().unsubscribe<event::ContactEnd>("voxel_contact_end");
		m_active_contacts.clear();
		physics::Simulator::unregisterVoxelNode(*this);
	}
	releaseVolume();
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterVoxelNodeProxy(this);
	m_registered_proxy = false;
}

void VoxelNode::destroy() {
	end();
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->unregisterDebugDraw(this);
	}
}

void VoxelNode::onEnable() {
	m_debug_visible = true;
	physics::Simulator::setBodyEnabled(m_body, true);
	physics::Simulator::setShapeEnabled(m_shape, true);
}

void VoxelNode::onDisable() {
	m_debug_visible = false;
	physics::Simulator::setBodyEnabled(m_body, false);
	physics::Simulator::setShapeEnabled(m_shape, false);
}

void VoxelNode::drawDebug() {
	ZoneScoped;
	if (!m_debug_visible || !show_aabb) {
		return;
	}

	const auto draw_bounds = [this](const physics::AABB& bounds, bool is_awake, const glm::vec4& base_color) {
		const glm::vec4 draw_color = is_awake ? base_color : glm::vec4(0.5f, 0.5f, 0.5f, base_color.a);
		const glm::mat4 transform =
		    glm::translate(glm::mat4(1.0f), (bounds.min + bounds.max) * 0.5f) * glm::scale(glm::mat4(1.0f), bounds.max - bounds.min);
		renderer::debugDrawShapeBox(transform, draw_color, aabb_fill);
	};

	if (const auto bounds = physics::Simulator::shapeWorldBounds(m_shape)) {
		draw_bounds(*bounds, awake, aabb_color);
	}

	static const glm::vec4 fragment_aabb_color {0.2f, 0.9f, 1.0f, 0.6f};
	for (const physics::VoxelRenderRecord& record : physics::Simulator::voxelFragmentRecords()) {
		if (record.fragment_origin == m_body) {
			draw_bounds(record.world_bounds, record.awake, fragment_aabb_color);
		}
	}
}

}
