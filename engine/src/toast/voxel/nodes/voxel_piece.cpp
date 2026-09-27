#include "voxel_piece.hpp"

#include "procedural_voxel.hpp"
#include "voxel_node_utils.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <toast/assets/script.hpp>
#include <toast/log.hpp>
#include <toast/voxel/stamp.hpp>
#include <tracy/Tracy.hpp>

namespace toast {

namespace {

using _detail::assetOfType;
using _detail::instantiateModel;
using _detail::toId;
using _detail::toVoxel;

[[nodiscard]]
auto brush(int id, voxel::WriteMode mode, int match_id) noexcept -> voxel::WriteBrush {
	return {.id = toId(id), .mode = mode, .match_id = toId(match_id)};
}

[[nodiscard]]
auto isTransformField(std::string_view field) noexcept -> bool {
	return field == "position" || field == "rotation" || field == "scale" || field == "world_position" ||
	       field == "world_rotation" || field == "world_scale";
}

}

template<typename Kernel>
void VoxelPiece::draw(std::string_view operation, Kernel&& kernel) {
	if (m_drawing == nullptr) {
		if (!m_warned_outside_edit) {
			m_warned_outside_edit = true;
			TOAST_WARN("Voxel", "'{}' {} only works inside editShape", name(), operation);
		}
		return;
	}
	const voxel::EditResult result = kernel(*m_drawing);
	if (result.pool_exhausted) {
		TOAST_WARN(
		    "Voxel",
		    "'{}' {} stopped early: the procedural brick pool is out of its {} bricks",
		    name(),
		    operation,
		    voxel::k_procedural_brick_capacity
		);
	}
}

auto VoxelPiece::getSize() -> glm::vec3 {
	return {pieceSize()};
}

auto VoxelPiece::pieceSize() -> glm::ivec3 {
	const voxel::Volume* current = m_drawing != nullptr ? m_drawing : grid();
	return current != nullptr ? glm::ivec3(current->voxelDims()) : glm::ivec3(0);
}

auto VoxelPiece::getVoxel(glm::vec3 pos) -> int {
	const voxel::Volume* current = m_drawing != nullptr ? m_drawing : grid();
	return current != nullptr ? current->materialAt(toVoxel(pos)) : 0;
}

void VoxelPiece::setVoxel(glm::vec3 pos, int id) {
	draw("setVoxel", [&](voxel::Volume& target) {
		voxel::EditResult result;
		const glm::ivec3 cell = toVoxel(pos);
		const voxel::Volume::VoxelWrite write = target.setVoxel(cell, toId(id));
		result.changed = write.changed ? 1 : 0;
		result.pool_exhausted = !write.changed && target.containsVoxel(cell) && write.previous_material != toId(id);
		return result;
	});
}

void VoxelPiece::removeVoxel(glm::vec3 pos) {
	setVoxel(pos, voxel::k_empty_palette_index);
}

void VoxelPiece::fillBox(glm::vec3 min, glm::vec3 max, int id, voxel::WriteMode mode, int match_id) {
	draw("fillBox", [&](voxel::Volume& target) {
		return voxel::fillBox(target, toVoxel(min), toVoxel(max), brush(id, mode, match_id));
	});
}

void VoxelPiece::fillRoundBox(glm::vec3 min, glm::vec3 max, float radius, int id, voxel::WriteMode mode, int match_id) {
	draw("fillRoundBox", [&](voxel::Volume& target) {
		return voxel::fillRoundBox(target, toVoxel(min), toVoxel(max), radius, brush(id, mode, match_id));
	});
}

void VoxelPiece::fillSphere(glm::vec3 center, float radius, int id, voxel::WriteMode mode, int match_id) {
	draw("fillSphere", [&](voxel::Volume& target) { return voxel::fillSphere(target, center, radius, brush(id, mode, match_id)); });
}

void VoxelPiece::fillEllipsoid(glm::vec3 min, glm::vec3 max, int id, voxel::WriteMode mode, int match_id) {
	draw("fillEllipsoid", [&](voxel::Volume& target) {
		return voxel::fillEllipsoid(target, toVoxel(min), toVoxel(max), brush(id, mode, match_id));
	});
}

void VoxelPiece::fillCylinder(glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode, int match_id) {
	draw("fillCylinder", [&](voxel::Volume& target) {
		return voxel::fillCylinder(target, a, b, radius, brush(id, mode, match_id));
	});
}

void VoxelPiece::fillCapsule(glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode, int match_id) {
	draw("fillCapsule", [&](voxel::Volume& target) { return voxel::fillCapsule(target, a, b, radius, brush(id, mode, match_id)); });
}

void VoxelPiece::carveBox(glm::vec3 min, glm::vec3 max) {
	fillBox(min, max, voxel::k_empty_palette_index);
}

void VoxelPiece::carveSphere(glm::vec3 center, float radius) {
	fillSphere(center, radius, voxel::k_empty_palette_index);
}

void VoxelPiece::paintBox(glm::vec3 min, glm::vec3 max, int id) {
	if (toId(id) != voxel::k_empty_palette_index) {
		fillBox(min, max, id, voxel::WriteMode::solid_only);
	}
}

void VoxelPiece::stamp(
    const assets::Handle<assets::VoxelModel>& asset, glm::vec3 pos, glm::quat rotation, voxel::WriteMode mode, int match_id
) {
	const assets::VoxelModel* model = assetOfType(asset, "voxel_model");
	if (model == nullptr) {
		TOAST_WARN("Voxel", "'{}' stamp needs a voxel model asset", name());
		return;
	}
	const std::unique_ptr<voxel::Volume> piece = instantiateModel(*model);
	if (piece == nullptr) {
		return;
	}
	const voxel::LatticePlacement placement {
	  .orientation = voxel::snapOrientation(glm::mat3_cast(glm::normalize(rotation))), .offset = toVoxel(pos)
	};
	draw("stamp", [&](voxel::Volume& target) {
		return voxel::stampVolume(target, *piece, placement, voxel::identityRemap(), mode, toId(match_id));
	});
}

void VoxelPiece::tile(
    const assets::Handle<assets::VoxelModel>& asset, int fit, glm::vec3 anchor, glm::vec3 offset, glm::vec3 mirror,
    voxel::WriteMode mode, int match_id
) {
	const assets::VoxelModel* model = assetOfType(asset, "voxel_model");
	if (model == nullptr) {
		TOAST_WARN("Voxel", "'{}' tile needs a voxel model asset", name());
		return;
	}
	const std::unique_ptr<voxel::Volume> pattern = instantiateModel(*model);
	if (pattern == nullptr) {
		return;
	}

	voxel::TileOptions options;
	options.fit = static_cast<voxel::TileFit>(std::clamp(fit, 0, 2));
	for (int axis = 0; axis < 3; ++axis) {
		options.anchor[static_cast<size_t>(axis)] =
		    static_cast<voxel::TileAnchor>(std::clamp(static_cast<int>(std::lround(anchor[axis])), 0, 2));
	}
	options.offset = glm::ivec3(glm::round(offset));
	options.mirror = glm::notEqual(mirror, glm::vec3(0.0f));

	draw("tile", [&](voxel::Volume& target) {
		const glm::ivec3 last = glm::ivec3(getSize()) - 1;
		return voxel::tileVolume(target, *pattern, glm::ivec3(0), last, options, voxel::identityRemap(), mode, toId(match_id));
	});
}

auto VoxelPiece::grid() -> const voxel::Volume* {
	if (m_grid_dirty) {
		ZoneScopedN("voxel::BuildPieceGrid");    // NOLINT
		ZoneNameF("%.*s", static_cast<int>(name().size()), name().data());
		m_clipped.reset();
		m_grid = prepareGrid();
		m_grid_dirty = false;
		m_clip_dirty = true;
		++m_build_count;
		if (m_grid != nullptr) {
			m_drawing = m_grid.get();
			call("editShape");
			m_drawing = nullptr;
			finishGrid(*m_grid);
			drawn.fire();
		}
	}

	if (m_grid == nullptr) {
		return nullptr;
	}

	if (m_clip_dirty) {
		m_clip_dirty = false;
		m_clipped.reset();
		if (!m_clip_planes.empty()) {
			m_clipped = std::make_unique<voxel::Volume>(voxel::Volume::instanceOf(*m_grid));
			voxel::clipByPlanes(*m_clipped, m_clip_planes);
		}
	}
	return m_clipped != nullptr ? m_clipped.get() : m_grid.get();
}

void VoxelPiece::redraw() {
	markGridDirty();
}

auto VoxelPiece::getShapeScript() const -> const assets::Handle<assets::Script>& {
	return m_shape_script;
}

auto VoxelPiece::getClipPlanes() const -> std::vector<glm::vec4> {
	return m_clip_planes;
}

void VoxelPiece::setClipPlanes(std::vector<glm::vec4> planes) {
	m_clip_planes = std::move(planes);
	m_clip_dirty = true;
	++m_input_revision;
}

void VoxelPiece::addClipPlane(glm::vec4 plane) {
	std::vector<glm::vec4> planes = m_clip_planes;
	planes.push_back(plane);
	setClipPlanes(std::move(planes));
}

void VoxelPiece::clearClipPlanes() {
	if (!m_clip_planes.empty()) {
		setClipPlanes({});
	}
}

auto VoxelPiece::getShapeBounds() -> std::vector<glm::vec3> {
	if (!insideProceduralVoxel(*this)) {
		return {};
	}
	const voxel::EditBounds bounds = pieceBounds(*this);
	return {glm::vec3(bounds.min), glm::vec3(bounds.max)};
}

auto VoxelPiece::getShape() -> Box<Node> {
	ProceduralVoxel* shape = owningProceduralVoxel(*this);
	return shape != nullptr ? shape->box() : Box<Node> {};
}

void VoxelPiece::releaseGrid() {
	m_clipped.reset();
	m_grid.reset();
	m_grid_dirty = true;
	m_clip_dirty = true;
}

void VoxelPiece::init() {
	// Older files only have the scripts list
	if (!scripts().empty() && m_shape_script.uid() != scripts().front().uid()) {
		m_shape_script = scripts().front();
	}
}

void VoxelPiece::setShapeScript(assets::Handle<assets::Script> script) {
	m_shape_script = std::move(script);
	setScripts(m_shape_script.hasValue() ? std::vector {m_shape_script} : std::vector<assets::Handle<assets::Script>> {});
	markGridDirty();
}

void VoxelPiece::setMisaligned(bool misaligned) {
	m_misaligned = misaligned;
}

void VoxelPiece::markGridDirty() noexcept {
	m_grid_dirty = true;
	++m_input_revision;
}

void VoxelPiece::onReflectedFieldChanged(std::string_view field_name) {
	Node3D::onReflectedFieldChanged(field_name);
	if (isTransformField(field_name)) {
		return;
	}
	if (field_name == "m_clip_planes") {
		m_clip_dirty = true;
		++m_input_revision;
		return;
	}
	if (field_name == "m_shape_script") {
		setShapeScript(m_shape_script);
		return;
	}
	markGridDirty();
}

void VoxelPiece::onScriptsReloaded() {
	if (!scripts().empty()) {
		m_shape_script = scripts().front();
	}
	markGridDirty();
}

void VoxelPiece::onScriptVarChanged(std::string_view /*path*/) {
	markGridDirty();
}

void VoxelPiece::updateInspectorMessages() {
	const NodeMessage outside {
	  .severity = NodeMessage::error, .id = _detail::k_message_outside, .text = "Only works inside a ProceduralVoxel"
	};
	if (insideProceduralVoxel(*this)) {
		removeInspectorMessage(outside);
	} else {
		addInspectorMessage(outside);
	}

	const NodeMessage misaligned {
	  .severity = NodeMessage::warning, .id = _detail::k_message_misaligned, .text = "Transform is snapped to closest voxel"
	};
	if (m_misaligned) {
		addInspectorMessage(misaligned);
	} else {
		removeInspectorMessage(misaligned);
	}
}

}
