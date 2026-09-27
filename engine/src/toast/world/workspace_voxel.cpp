#include "camera.hpp"
#include "mesh_node.hpp"
#include "workspace.hpp"
#include "workspace_events.hpp"

#include <algorithm>
#include <toast/assets/asset_manager.hpp>
#include <toast/engine.hpp>
#include <toast/log.hpp>
#include <toast/renderer/editor_overlays.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/voxel/assets/voxel_model.hpp>
#include <toast/voxel/nodes/carve_volume.hpp>
#include <toast/voxel/nodes/fill_volume.hpp>
#include <toast/voxel/nodes/paint_volume.hpp>
#include <toast/voxel/nodes/procedural_voxel.hpp>
#include <toast/voxel/nodes/voxel_mesh.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/window/window_events.hpp>
#include <tracy/Tracy.hpp>

namespace toast {

namespace {

[[nodiscard]]
auto voxelHistory(event::HistoryOperation operation, const Box<Node>& node, std::string subject) -> WorkspaceHistory::Context {
	WorkspaceHistory::Context context;
	context.operation = operation;
	if (node.exists()) {
		context.node = node->uid();
		context.node_name = node->name();
	}
	context.subject = std::move(subject);
	return context;
}

[[nodiscard]]
auto voxelPieceOf(const Box<Node>& node) -> VoxelPiece* {
	return node.exists() ? reflect_cast<VoxelPiece>(&const_cast<Node&>(*node)) : nullptr;
}

[[nodiscard]]
auto voxelFloorHalf(int32_t value) noexcept -> int32_t {
	return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

/// turns quarter turns around a shape axis, exact so snapping never drifts
[[nodiscard]]
auto voxelQuarterTurn(uint32_t axis, int32_t turns) -> glm::mat3 {
	glm::mat3 out(1.0f);
	const int32_t steps = ((turns % 4) + 4) % 4;
	const int a = static_cast<int>((axis + 1) % 3);
	const int b = static_cast<int>((axis + 2) % 3);
	for (int32_t i = 0; i < steps; ++i) {
		// One quarter turn sends a to b and b to minus a
		glm::mat3 quarter(1.0f);
		quarter[a][a] = 0.0f;
		quarter[b][b] = 0.0f;
		quarter[a][b] = 1.0f;
		quarter[b][a] = -1.0f;
		out = quarter * out;
	}
	return out;
}

constexpr std::array<std::string_view, 3> k_volume_types {"toast::FillVolume", "toast::CarveVolume", "toast::PaintVolume"};

}

auto Workspace::findPrefabInstance(UID prefab, UID instance) -> Box<Node> {
	if (!m_root_node.exists()) {
		return {};
	}
	if (instance.data() != 0) {
		if (Box<Node> found = findFrom(*m_root_node, instance); found.exists()) {
			return found;
		}
	}
	const auto search = [&](this auto&& self, const Box<Node>& node) -> Box<Node> {
		if (node->isInstanceRoot() && node->sourcePrefab().uid() == prefab) {
			return node;
		}
		for (const Box<Node>& child : node->children()) {
			if (Box<Node> found = self(child); found.exists()) {
				return found;
			}
		}
		return {};
	};
	return search(m_root_node);
}

// 3D viewport tools

namespace {

enum : uint8_t {
	k_tool_select = 0,
	k_tool_move = 1,
	k_tool_extrude = 2,
	k_tool_buildup = 3,
	k_tool_carve = 4,
	k_tool_paint = 5,
	k_tool_bucket = 6,
	k_tool_split = 7,
	k_tool_slice = 8,
};

constexpr uint32_t k_left_button = 1;
constexpr int k_shift_mod = 0x0001;    ///< SDL left shift, the editor sends any shift as it
constexpr int k_escape_key = 27;       ///< SDLK_ESCAPE
constexpr int k_tab_key = 9;           ///< SDLK_TAB

/** Preview kind a box tool draws with, 0 fill, 1 carve and 2 paint */
[[nodiscard]]
auto boxToolKind(uint32_t tool) noexcept -> uint8_t {
	if (tool == k_tool_carve) {
		return 1;
	}
	if (tool == k_tool_paint) {
		return 2;
	}
	return 0;
}

/** Node type a box tool creates */
[[nodiscard]]
auto boxToolNodeType(uint32_t tool) noexcept -> const char* {
	if (tool == k_tool_carve) {
		return "toast::CarveVolume";
	}
	if (tool == k_tool_paint) {
		return "toast::PaintVolume";
	}
	return "toast::FillVolume";
}

/** Outline color of a box tool */
[[nodiscard]]
auto boxToolColor(uint32_t tool) noexcept -> glm::vec4 {
	if (tool == k_tool_carve) {
		return {1.0f, 0.35f, 0.2f, 1.0f};
	}
	if (tool == k_tool_paint) {
		return {0.2f, 0.6f, 1.0f, 1.0f};
	}
	return {0.3f, 1.0f, 0.4f, 1.0f};
}

struct ShapeRay {
	glm::vec3 origin;       ///< In shape voxels
	glm::vec3 direction;    ///< In shape voxels per world metre
};

[[nodiscard]]
auto voxelMouseRay(const glm::vec2& mouse) -> std::optional<Ray> {
	Camera* camera = renderer::getActiveCamera();
	if (camera == nullptr) {
		return std::nullopt;
	}
	const auto extent = renderer::getOutputTarget().getExtent();
	return camera->screenPointToRay(mouse, {static_cast<float>(extent.width), static_cast<float>(extent.height)});
}

[[nodiscard]]
auto voxelShapeRay(ProceduralVoxel& shape, const Ray& ray) -> ShapeRay {
	shape.syncTransform();
	const glm::mat4 to_shape = glm::inverse(shape.getWorldTransform());
	return {
	  glm::vec3(to_shape * glm::vec4(ray.origin, 1.0f)) / voxel::k_voxel_size,
	  glm::vec3(to_shape * glm::vec4(ray.direction, 0.0f)) / voxel::k_voxel_size
	};
}

[[nodiscard]]
auto voxelCellOnPlane(const ShapeRay& ray, int axis, float coordinate) -> std::optional<glm::ivec3> {
	if (std::abs(ray.direction[axis]) < 1e-6f) {
		return std::nullopt;
	}
	const float t = (coordinate - ray.origin[axis]) / ray.direction[axis];
	if (t < 0.0f) {
		return std::nullopt;
	}
	return glm::ivec3(glm::floor(ray.origin + ray.direction * t));
}

[[nodiscard]]
auto voxelShapeHit(ProceduralVoxel& shape, const ShapeRay& ray) -> std::optional<voxel::VolumeHit> {
	const voxel::Volume* volume = shape.volume();
	if (volume == nullptr) {
		return std::nullopt;
	}
	std::optional<voxel::VolumeHit> hit = voxel::raycast(*volume, ray.origin - glm::vec3(shape.voxelOrigin()), ray.direction, 1e6f);
	if (hit.has_value()) {
		hit->voxel += shape.voxelOrigin();
		if (hit->normal == glm::ivec3(0)) {
			hit->normal = -voxel::dominantAxis(ray.direction);
		}
	}
	return hit;
}

}

auto Workspace::voxelToolShape() -> ProceduralVoxel* {
	if (m_focused_node.exists()) {
		if (auto* shape = reflect_cast<ProceduralVoxel>(&*m_focused_node)) {
			return shape;
		}
		if (auto* owner = owningProceduralVoxel(*m_focused_node)) {
			return owner;
		}
	}
	return m_root_node.exists() ? reflect_cast<ProceduralVoxel>(&*m_root_node) : nullptr;
}

auto Workspace::VoxelToolState::box() const -> std::pair<glm::ivec3, glm::ivec3> {
	glm::ivec3 min = glm::min(anchor, current);
	glm::ivec3 max = glm::max(anchor, current);
	const int32_t first = depth > 0 ? surface + sign : surface;
	const int32_t last = depth > 0 ? first + (sign * (depth - 1)) : first - (sign * (-depth - 1));
	min[axis] = std::min(first, last);
	max[axis] = std::max(first, last);
	return {min, max};
}

void Workspace::voxelReparent(Box<Node> node, Box<Node> parent, size_t index) {
	if (Box<Node> old = node->parentInternal(); old.exists()) {
		std::erase(old->m_children, node);
	}
	node->m_parent = parent;
	auto& children = parent->m_children;
	children.insert(children.begin() + static_cast<std::ptrdiff_t>(std::min(index, children.size())), node);

	const auto refresh = [](this auto&& self, const Box<Node>& current) -> void {
		if (auto spatial = current.as<Node3D>(); spatial.exists()) {
			spatial->refreshTransformParent();
		}
		for (const Box<Node>& child : current->children()) {
			self(child);
		}
	};
	refresh(node);
}

auto Workspace::voxelWrapInGroup(Box<Node> node, std::string_view name) -> Box<Node> {
	Box<Node> parent = node->parentInternal();
	const auto& siblings = parent->m_children;
	const auto index = static_cast<size_t>(std::ranges::find(siblings, node) - siblings.begin());
	Box<Node> group = requestRuntimeCreate(parent, "toast::VoxelGroup");
	group->name(uniqueChildName(*parent, name));
	voxelReparent(group, parent, index);
	voxelReparent(node, group, 0);
	return group;
}

namespace {

struct BoxEntry {
	glm::vec3 point;
	int axis = 0;
	int32_t sign = 1;
};

[[nodiscard]]
auto voxelRayBox(const glm::vec3& origin, const glm::vec3& direction, glm::vec3 lo, glm::vec3 hi) -> std::optional<BoxEntry> {
	float t_enter = -std::numeric_limits<float>::max();
	float t_exit = std::numeric_limits<float>::max();
	int axis = -1;
	for (int a = 0; a < 3; ++a) {
		if (std::abs(direction[a]) < 1e-9f) {
			if (origin[a] < lo[a] || origin[a] > hi[a]) {
				return std::nullopt;
			}
			continue;
		}
		float t0 = (lo[a] - origin[a]) / direction[a];
		float t1 = (hi[a] - origin[a]) / direction[a];
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		if (t0 > t_enter) {
			t_enter = t0;
			axis = a;
		}
		t_exit = std::min(t_exit, t1);
	}
	if (axis < 0 || t_enter > t_exit || t_exit < 0.0f || t_enter < 0.0f) {
		return std::nullopt;
	}
	return BoxEntry {origin + direction * t_enter, axis, direction[axis] > 0.0f ? -1 : 1};
}

}

void Workspace::voxelToolCancel() {
	m_voxel_tool.phase = VoxelToolState::Phase::idle;
	m_voxel_tool.dragging = false;
	renderer::EditorOverlays& overlays = renderer::editorOverlays();
	overlays.tool_box_active = false;
	overlays.tool_box2_active = false;
	overlays.tool_piece = nullptr;
	overlays.cut_active = false;
}

void Workspace::voxelUpdatePreview(uint8_t kind) {
	renderer::EditorOverlays& overlays = renderer::editorOverlays();
	const auto [min, max] = m_voxel_tool.box();
	overlays.tool_box_active = true;
	overlays.tool_box_min = min;
	overlays.tool_box_max = max;
	overlays.tool_box_kind = kind;
	if (kind > 2) {
		overlays.tool_piece = nullptr;
		return;
	}

	if (!m_voxel_preview.exists()) {
		m_voxel_preview = nodeAllocation("toast::FillVolume");
	}
	auto* volume = m_voxel_preview.exists() ? reflect_cast<VoxelVolume>(&*m_voxel_preview) : nullptr;
	if (volume == nullptr) {
		overlays.tool_piece = nullptr;
		return;
	}

	const glm::ivec3 size = max - min + 1;
	if (volume->sizeVoxels() != size) {
		volume->setSizeVoxels(size);
	}
	int colour = m_voxel_tool.paint_id;
	if (m_voxel_tool.tool == k_tool_extrude) {
		if (auto* source = reflect_cast<VoxelVolume>(voxelPieceOf(findFrom(m_root_node, m_voxel_tool.extrude_source)))) {
			colour = source->getId();
		}
	}
	volume->setId(colour);
	const assets::Handle<assets::Script> script = m_voxel_tool.default_script.data() != 0
	                                                  ? assets::load<assets::Script>(m_voxel_tool.default_script)
	                                                  : assets::load<assets::Script>("core://VoxelScripts/DrawBox.lua");
	if (volume->scripts().empty() || volume->scripts().front().uid() != script.uid()) {
		volume->setShapeScript(script);
	}
	overlays.tool_piece = volume;
}

void Workspace::voxelToolMouseMove() {
	ProceduralVoxel* shape = voxelToolShape();
	const std::optional<Ray> ray = voxelMouseRay(m_gizmo_mouse_pos);
	if (shape == nullptr || !ray.has_value()) {
		return;
	}
	renderer::EditorOverlays& overlays = renderer::editorOverlays();
	const ShapeRay local = voxelShapeRay(*shape, *ray);
	VoxelToolState& tool = m_voxel_tool;

	if (tool.tool == k_tool_extrude && (tool.phase == VoxelToolState::Phase::idle || tool.phase == VoxelToolState::Phase::face)) {
		VoxelPiece* piece = m_focused_node.exists() ? reflect_cast<VoxelPiece>(&*m_focused_node) : nullptr;
		const voxel::EditBounds bounds = piece != nullptr ? pieceBounds(*piece) : voxel::EditBounds {};
		const std::optional<BoxEntry> entry =
		    piece != nullptr ? voxelRayBox(local.origin, local.direction, glm::vec3(bounds.min), glm::vec3(bounds.max + 1))
				                 : std::nullopt;
		if (!entry.has_value()) {
			voxelToolCancel();
			return;
		}

		tool.phase = VoxelToolState::Phase::face;
		tool.extrude_source = piece->uid();
		tool.axis = entry->axis;
		tool.sign = entry->sign;
		tool.surface = entry->sign > 0 ? bounds.max[entry->axis] : bounds.min[entry->axis];
		tool.plane = static_cast<float>(entry->sign > 0 ? bounds.max[entry->axis] + 1 : bounds.min[entry->axis]);
		tool.face_min = bounds.min;
		tool.face_max = bounds.max;
		tool.depth = 1;

		const int cut = (tool.axis + 1 + tool.cut_axis) % 3;
		tool.cut_at = std::clamp(static_cast<int32_t>(std::lround(entry->point[cut])), bounds.min[cut], bounds.max[cut] + 1);

		overlays.tool_box_color = glm::vec4(1.0f, 0.639f, 0.0f, 1.0f);
		// Both parts of the face
		tool.anchor = bounds.min;
		tool.current = bounds.max;
		tool.current[cut] = tool.cut_at - 1;
		const bool lower = tool.cut_at > bounds.min[cut];
		const bool upper = tool.cut_at <= bounds.max[cut];
		if (!lower) {
			tool.current = bounds.max;
		}
		overlays.tool_root = shape->uid();
		voxelUpdatePreview(3);
		overlays.tool_box2_active = lower && upper;
		if (overlays.tool_box2_active) {
			glm::ivec3 other_min = bounds.min;
			other_min[cut] = tool.cut_at;
			VoxelToolState other = tool;
			other.anchor = other_min;
			other.current = bounds.max;
			std::tie(overlays.tool_box2_min, overlays.tool_box2_max) = other.box();
		}
		return;
	}

	if (tool.phase == VoxelToolState::Phase::part) {
		// The part of the face under the mouse is the one that grows
		const int cut = (tool.axis + 1 + tool.cut_axis) % 3;
		const std::optional<glm::ivec3> cell = voxelCellOnPlane(local, tool.axis, tool.plane);
		const bool lower = tool.cut_at > tool.face_min[cut];
		const bool upper = tool.cut_at <= tool.face_max[cut];
		const bool pick_lower = lower && (!upper || (cell.has_value() && (*cell)[cut] < tool.cut_at));
		tool.anchor = tool.face_min;
		tool.current = tool.face_max;
		if (pick_lower) {
			tool.current[cut] = tool.cut_at - 1;
		} else {
			tool.anchor[cut] = tool.cut_at;
		}
		overlays.tool_box2_active = false;
		voxelUpdatePreview(3);
		return;
	}

	if (tool.phase == VoxelToolState::Phase::box) {
		if (std::optional<glm::ivec3> cell = voxelCellOnPlane(local, tool.axis, tool.plane)) {
			tool.current = glm::clamp(*cell, tool.face_min, tool.face_max);
		}
	} else if (tool.phase == VoxelToolState::Phase::height) {
		// Closest point between the mouse ray and the line out of the footprint centre
		const glm::ivec3 min = glm::min(tool.anchor, tool.current);
		const glm::ivec3 max = glm::max(tool.anchor, tool.current);
		glm::vec3 centre = (glm::vec3(min) + glm::vec3(max + 1)) * 0.5f;
		centre[tool.axis] = tool.plane;
		glm::vec3 line(0.0f);
		line[tool.axis] = static_cast<float>(tool.sign);

		const glm::vec3 w = centre - local.origin;
		const float b = glm::dot(line, local.direction);
		const float c = glm::dot(local.direction, local.direction);
		const float denominator = c - (b * b);
		if (std::abs(denominator) > 1e-6f) {
			const float along = ((b * glm::dot(local.direction, w)) - (c * glm::dot(line, w))) / denominator;
			const auto layers = static_cast<int32_t>(std::lround(along));
			if (tool.tool == k_tool_extrude) {
				// Pulling out grows a new box, pushing in carves one
				tool.depth = layers >= 0 ? std::max(1, layers) : std::min(-1, layers);
			} else if (tool.tool == k_tool_buildup) {
				tool.depth = std::max(1, layers);
			} else {
				tool.depth = std::min(-1, layers);
			}
		}
	}

	if (tool.phase == VoxelToolState::Phase::box || tool.phase == VoxelToolState::Phase::height) {
		overlays.tool_root = shape->uid();
		// Extruding out previews as a fill, pushing in as a carve
		if (tool.tool == k_tool_extrude) {
			voxelUpdatePreview(tool.depth > 0 ? uint8_t {0} : uint8_t {1});
		} else {
			voxelUpdatePreview(boxToolKind(tool.tool));
		}
		return;
	}

	if (tool.phase == VoxelToolState::Phase::cut) {
		// The plane through the eye and the two mouse rays
		const std::optional<Ray> start = voxelMouseRay(tool.cut_start);
		if (!start.has_value() || glm::distance(tool.cut_start, m_gizmo_mouse_pos) < 4.0f) {
			return;
		}
		const ShapeRay a = voxelShapeRay(*shape, *start);
		const glm::vec3 normal = glm::cross(a.direction, local.direction);
		if (glm::dot(normal, normal) < 1e-12f) {
			return;
		}
		const glm::vec3 n = glm::normalize(normal);
		tool.cut_plane = glm::vec4(n, -glm::dot(n, a.origin));
		overlays.cut_active = true;
		overlays.cut_root = shape->uid();
		overlays.cut_plane = tool.cut_plane;
	}
}

auto Workspace::voxelToolMouseButton(uint32_t button, bool pressed, int mods) -> bool {
	const uint32_t tool = m_voxel_tool.tool;
	if (button != k_left_button) {
		return false;
	}
	ProceduralVoxel* shape = voxelToolShape();
	const std::optional<Ray> ray = voxelMouseRay(m_gizmo_mouse_pos);
	if (shape == nullptr || !ray.has_value()) {
		return false;
	}
	const ShapeRay local = voxelShapeRay(*shape, *ray);
	const bool box_tool = tool == k_tool_buildup || tool == k_tool_carve || tool == k_tool_paint;
	const bool cut_tool = tool == k_tool_split || tool == k_tool_slice;

	// Select and Move pick the piece under the mouse, Move only when no gizmo handle was hit first
	if (tool == k_tool_select || tool == k_tool_move) {
		// Only the VoxelEditor, a level keeps its own selection rules
		if (!pressed || !m_root_node.exists() || reflect_cast<ProceduralVoxel>(&*m_root_node) == nullptr) {
			return false;
		}
		const std::optional<voxel::VolumeHit> hit = voxelShapeHit(*shape, local);
		VoxelPiece* piece = hit.has_value() ? shape->pieceAt(hit->voxel) : nullptr;
		m_focused_node = piece != nullptr ? piece->box() : Box<Node> {};
		renderer::editorOverlays().selected = piece != nullptr ? piece->uid() : UID {};
		event::NodePicked picked;
		picked.workspace_handle = m_handle.data();
		picked.node = piece != nullptr ? piece->uid() : UID {};
		event::send<event::NodePicked>(picked);
		return true;
	}

	// A cut only goes through the selected piece
	const auto cut_targets = [&] {
		std::vector<UID> targets;
		if (m_focused_node.exists() && reflect_cast<VoxelPiece>(&*m_focused_node) != nullptr) {
			targets.push_back(m_focused_node->uid());
		}
		return targets;
	};

	// Places whatever the box tool drew
	const auto place = [&] {
		const auto [min, max] = m_voxel_tool.box();
		if (tool == k_tool_extrude) {
			event::VoxelExtrude extrude;
			extrude.source = m_voxel_tool.extrude_source;
			extrude.min = min;
			extrude.max = max;
			extrude.inward = m_voxel_tool.depth < 0;
			event::send<event::VoxelExtrude>(extrude);
		} else {
			event::VoxelCreatePiece create;
			const bool in_group = m_focused_node.exists() && reflect_cast<VoxelGroup>(&*m_focused_node) != nullptr;
			create.parent = in_group ? m_focused_node->uid() : shape->uid();
			create.type = boxToolNodeType(tool);
			create.min = min;
			create.max = max;
			create.script = m_voxel_tool.default_script;
			event::send<event::VoxelCreatePiece>(create);
		}
		voxelToolCancel();
	};

	switch (m_voxel_tool.phase) {
		case VoxelToolState::Phase::pick_side:
			// Click keeps red, shift click keeps blue
			if (pressed) {
				event::VoxelSplitPieces slice;
				slice.targets = cut_targets();
				slice.plane = m_voxel_tool.cut_plane;
				slice.keep = (mods & k_shift_mod) != 0 ? 2 : 1;
				event::send<event::VoxelSplitPieces>(slice);
				voxelToolCancel();
			}
			return true;

		case VoxelToolState::Phase::face:
			if (pressed) {
				m_voxel_tool.phase = VoxelToolState::Phase::part;
				voxelToolMouseMove();
			}
			return true;

		case VoxelToolState::Phase::part:
			if (pressed) {
				m_voxel_tool.phase = VoxelToolState::Phase::height;
				m_voxel_tool.dragging = true;
				m_voxel_tool.depth = 1;
				voxelToolMouseMove();
			}
			return true;

		case VoxelToolState::Phase::height:
			if (pressed && !m_voxel_tool.dragging) {
				place();
			} else if (!pressed && m_voxel_tool.dragging) {
				// Released without pulling, the height follows the mouse until the next click
				if (m_voxel_tool.depth == 1) {
					m_voxel_tool.dragging = false;
				} else {
					place();
				}
			}
			return true;

		case VoxelToolState::Phase::box:
			if (!pressed && m_voxel_tool.dragging) {
				// trackpad support, dragging is akward
				if (m_voxel_tool.current == m_voxel_tool.anchor) {
					m_voxel_tool.dragging = false;
				} else {
					m_voxel_tool.phase = VoxelToolState::Phase::height;
					m_voxel_tool.dragging = false;
				}
			} else if (pressed && !m_voxel_tool.dragging) {
				m_voxel_tool.phase = VoxelToolState::Phase::height;
			}
			return true;

		case VoxelToolState::Phase::cut:
			if (!pressed) {
				if (renderer::editorOverlays().cut_active && tool == k_tool_split) {
					event::VoxelSplitPieces split;
					split.targets = cut_targets();
					split.plane = m_voxel_tool.cut_plane;
					event::send<event::VoxelSplitPieces>(split);
					voxelToolCancel();
				} else if (renderer::editorOverlays().cut_active) {
					m_voxel_tool.phase = VoxelToolState::Phase::pick_side;
				} else {
					voxelToolCancel();
				}
			}
			return true;

		case VoxelToolState::Phase::idle: break;
	}

	if (!box_tool && !cut_tool && tool != k_tool_bucket) {
		return tool == k_tool_extrude;
	}
	if (!pressed) {
		return true;
	}

	// Pressed with nothing going on

	if (cut_tool) {
		if (!cut_targets().empty()) {
			m_voxel_tool.phase = VoxelToolState::Phase::cut;
			m_voxel_tool.cut_start = m_gizmo_mouse_pos;
		}
		return true;
	}

	const std::optional<voxel::VolumeHit> hit = voxelShapeHit(*shape, local);

	if (tool == k_tool_bucket) {
		if (hit.has_value()) {
			event::VoxelBucketFill fill;
			fill.target = shape->uid();
			fill.voxel = hit->voxel;
			fill.id = m_voxel_tool.paint_id;
			event::send<event::VoxelBucketFill>(fill);
		}
		return true;
	}

	m_voxel_tool.face_min = glm::ivec3(std::numeric_limits<int32_t>::min());
	m_voxel_tool.face_max = glm::ivec3(std::numeric_limits<int32_t>::max());
	if (hit.has_value()) {
		int axis = 2;
		if (hit->normal.x != 0) {
			axis = 0;
		} else if (hit->normal.y != 0) {
			axis = 1;
		}
		m_voxel_tool.axis = axis;
		m_voxel_tool.sign = hit->normal[axis];
		m_voxel_tool.surface = hit->voxel[axis];
		m_voxel_tool.plane = static_cast<float>(hit->voxel[axis] + (m_voxel_tool.sign > 0 ? 1 : 0));
		m_voxel_tool.anchor = hit->voxel;
	} else {
		const std::optional<glm::ivec3> ground = voxelCellOnPlane(local, 2, 0.0f);
		if (!ground.has_value()) {
			return true;
		}
		// The ground is the top of a face under z 0 so buildup starts on layer 0
		m_voxel_tool.axis = 2;
		m_voxel_tool.sign = 1;
		m_voxel_tool.surface = -1;
		m_voxel_tool.plane = 0.0f;
		m_voxel_tool.anchor = *ground;
	}
	m_voxel_tool.current = m_voxel_tool.anchor;
	m_voxel_tool.depth = tool == k_tool_buildup ? 1 : -1;
	m_voxel_tool.dragging = true;
	m_voxel_tool.phase = VoxelToolState::Phase::box;

	renderer::EditorOverlays& overlays = renderer::editorOverlays();
	overlays.tool_root = shape->uid();
	overlays.tool_box_color = boxToolColor(tool);
	voxelUpdatePreview(boxToolKind(tool));
	return true;
}

void Workspace::subscribeVoxelEditing() {
	const auto active = [this] { return m_handle.data() == Engine::get()->activeWorkspace().data(); };

	m_listener.subscribe<event::SetVoxelTool>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		if (e.tool != m_voxel_tool.tool) {
			voxelToolCancel();
		}
		m_voxel_tool.tool = e.tool;
		m_voxel_tool.paint_id = static_cast<uint8_t>(std::clamp(e.paint_id, 1u, 255u));
		m_voxel_tool.default_script = e.default_script;
		if (auto* shape = m_root_node.exists() ? reflect_cast<ProceduralVoxel>(&*m_root_node) : nullptr) {
			shape->sendLayout();
		}
		return true;
	});

	// Escape drops whatever the 3D tool was in the middle of
	m_listener.subscribe<event::WindowKey>("voxel_tool_escape", [this, active](const auto& e) {
		if (active() && e.key == k_escape_key && e.action == 1 && m_voxel_tool.phase != VoxelToolState::Phase::idle) {
			voxelToolCancel();
		}
		// Tab swaps which way the Extrude cut runs across the face
		if (active() && e.key == k_tab_key && e.action == 1 && m_voxel_tool.phase == VoxelToolState::Phase::face) {
			m_voxel_tool.cut_axis = 1 - m_voxel_tool.cut_axis;
			voxelToolMouseMove();
		}
		return false;
	});

	// Overlays are one global state for the viewport, only the active workspace applies them
	m_listener.subscribe<event::SetVoxelEditorOverlays>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		renderer::EditorOverlays& overlays = renderer::editorOverlays();
		overlays.surface_unit_grid = e.unit_grid;
		overlays.surface_voxel_grid = e.voxel_grid;
		overlays.volume_edges = e.edges;
		overlays.voxel_edges = e.voxel_edges;
		overlays.projections = e.projections;
		if (auto* shape = m_root_node.exists() ? reflect_cast<ProceduralVoxel>(&*m_root_node) : nullptr) {
			shape->sendLayout();
		}
		return true;
	});

	m_listener.subscribe<event::SetVoxelCutPreview>([active](const auto& e) {
		if (!active()) {
			return false;
		}
		renderer::EditorOverlays& overlays = renderer::editorOverlays();
		overlays.cut_root = e.root;
		overlays.cut_active = e.active;
		overlays.cut_plane = e.plane;
		return true;
	});

	m_listener.subscribe<event::VoxelSetPieceBounds>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		auto target = findFrom(m_root_node, e.target);
		VoxelPiece* piece = voxelPieceOf(target);
		if (piece == nullptr) {
			TOAST_WARN("Voxel", "VoxelSetPieceBounds: {} is not a piece", e.target.get());
			return true;
		}
		recordHistory(voxelHistory(event::HistoryOperation::change_value, target, "Volume resized"), [&] {
			placePiece(*piece, piecePlacement(*piece).orientation, e.min, e.max);
		});
		return true;
	});

	m_listener.subscribe<event::VoxelCreatePiece>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		auto source = e.source.data() != 0 ? findFrom(m_root_node, e.source) : Box<Node> {};
		auto parent = findFrom(m_root_node, e.parent);
		if (voxelPieceOf(source) == nullptr) {
			const bool valid_parent = parent.exists() && (reflect_cast<ProceduralVoxel>(&*parent) != nullptr ||
			                                              reflect_cast<VoxelGroup>(&*parent) != nullptr);
			if (!valid_parent || std::ranges::find(k_volume_types, e.type) == k_volume_types.end()) {
				TOAST_WARN("Voxel", "VoxelCreatePiece: needs a ProceduralVoxel or group parent and a volume type");
				return true;
			}
		}

		recordHistory(voxelHistory(event::HistoryOperation::create, source, "Volume added"), [&] {
			Box<Node> made;
			voxel::LatticeOrientation orientation;
			if (VoxelPiece* from = voxelPieceOf(source)) {
				// Extrude copies everything from the source and sits right after it
				orientation = piecePlacement(*from).orientation;
				made = duplicateNode(source, source->parentInternal(), true);
			} else {
				made = requestRuntimeCreate(parent, e.type);
			}
			if (VoxelPiece* piece = voxelPieceOf(made)) {
				piece->setClipPlanes({});
				placePiece(*piece, orientation, e.min, e.max);
				if (e.script.data() != 0) {
					piece->setShapeScript(assets::load<assets::Script>(e.script));
				}
			}
		});
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	// Blender style extrude of part of a face
	m_listener.subscribe<event::VoxelExtrude>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		auto source = findFrom(m_root_node, e.source);
		VoxelPiece* from = voxelPieceOf(source);
		if (from == nullptr) {
			TOAST_WARN("Voxel", "VoxelExtrude: {} is not a piece", e.source.get());
			return true;
		}

		recordHistory(voxelHistory(event::HistoryOperation::create, source, e.inward ? "Extruded in" : "Extruded"), [&] {
			// Extruding again from an extrusion keeps adding to the same group
			Box<Node> parent = source->parentInternal();
			Box<Node> group = reflect_cast<VoxelGroup>(&*parent) != nullptr
			                      ? parent
			                      : voxelWrapInGroup(source, std::string(source->name()) + " extrusion");

			Box<Node> made = requestRuntimeCreate(group, e.inward ? "toast::CarveVolume" : "toast::FillVolume");
			if (auto* volume = made.exists() ? reflect_cast<VoxelVolume>(&*made) : nullptr) {
				volume->name(e.inward ? "Extruded in" : "Extruded");
				if (m_voxel_tool.default_script.data() != 0) {
					volume->setShapeScript(assets::load<assets::Script>(m_voxel_tool.default_script));
				}
				if (auto* source = reflect_cast<VoxelVolume>(from)) {
					volume->setId(source->getId());
				} else {
					volume->setId(m_voxel_tool.paint_id);
				}
				placePiece(*volume, {}, e.min, e.max);
			}
		});
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	m_listener.subscribe<event::VoxelSplitPieces>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		if (glm::dot(glm::vec3(e.plane), glm::vec3(e.plane)) <= 0.0f) {
			return true;
		}
		const char* subject = e.keep == 0 ? "Split" : "Slice";
		recordHistory(voxelHistory(event::HistoryOperation::duplicate, {}, subject), [&] {
			for (const UID& uid : e.targets) {
				auto target = findFrom(m_root_node, uid);
				VoxelPiece* piece = voxelPieceOf(target);
				if (piece == nullptr) {
					continue;
				}
				const glm::vec4 plane = planeToPiece(e.plane, piecePlacement(*piece));

				// A cut that leaves one side empty is not a cut
				if (const voxel::Volume* grid = piece->grid()) {
					const auto voxels_kept = [&](glm::vec4 side) {
						voxel::Volume half = voxel::Volume::instanceOf(*grid);
						const std::array<glm::vec4, 1> planes {side};
						voxel::clipByPlanes(half, planes);
						return half.solidVoxelCount();
					};
					if (voxels_kept(plane) == 0 || voxels_kept(voxel::complementPlane(plane)) == 0) {
						continue;
					}
				}

				// Split keeps both halves together in a group, slice hangs the dropped half off the kept one
				const Box<Node> parent =
				    e.keep == 0 ? voxelWrapInGroup(target, std::string(target->name()) + " split") : target->parentInternal();
				Box<Node> copy = duplicateNode(target, parent, true);
				VoxelPiece* other = voxelPieceOf(copy);
				if (other == nullptr) {
					continue;
				}

				std::vector<glm::vec4> kept = piece->clipPlanes();
				std::vector<glm::vec4> rest = kept;
				kept.push_back(plane);
				rest.push_back(voxel::complementPlane(plane));
				piece->setClipPlanes(std::move(kept));
				other->setClipPlanes(std::move(rest));

				// Slice keeps one side, the other is only disabled so it can come back
				if (e.keep != 0) {
					Box<Node> kept_box = e.keep == 1 ? target : copy;
					Box<Node> dropped_box = e.keep == 1 ? copy : target;
					dropped_box->enabled(false);
					voxelReparent(dropped_box, kept_box, kept_box->children().size());
					// Both sat in the same place so under the kept one it sits at its origin
					if (auto* dropped = voxelPieceOf(dropped_box)) {
						dropped->position = glm::vec3(0.0f);
						dropped->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
						dropped->scale = glm::vec3(1.0f);
						dropped->syncTransform();
					}
				}
			}
		});
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	m_listener.subscribe<event::VoxelRotatePieces>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		const glm::mat3 turn = voxelQuarterTurn(std::min(e.axis, 2u), e.turns);
		recordHistory(voxelHistory(event::HistoryOperation::change_value, {}, "Rotated"), [&] {
			for (const UID& uid : e.targets) {
				VoxelPiece* piece = voxelPieceOf(findFrom(m_root_node, uid));
				if (piece == nullptr) {
					continue;
				}
				// Turn in place around the centre of what it covers
				const voxel::EditBounds before = pieceBounds(*piece);
				const voxel::LatticeOrientation turned =
				    voxel::snapOrientation(turn * voxel::toMatrix(piecePlacement(*piece).orientation));
				const glm::ivec3 size = piece->nominalSize();
				const glm::ivec3 centre_twice = before.min + before.max + 1;
				glm::ivec3 extent;
				for (int axis = 0; axis < 3; ++axis) {
					extent[axis] = size[turned.source[static_cast<size_t>(axis)]];
				}
				glm::ivec3 min;
				for (int axis = 0; axis < 3; ++axis) {
					min[axis] = voxelFloorHalf(centre_twice[axis] - extent[axis]);
				}
				placePiece(*piece, turned, min, min + extent - 1);
			}
		});
		return true;
	});

	// Bucket adds a VoxelBucket node
	m_listener.subscribe<event::VoxelBucketFill>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}
		auto target = findFrom(m_root_node, e.target);
		ProceduralVoxel* shape = target.exists() ? reflect_cast<ProceduralVoxel>(&*target) : nullptr;
		if (shape == nullptr && target.exists()) {
			shape = owningProceduralVoxel(*target);
		}
		const voxel::Volume* volume = shape != nullptr ? shape->volume() : nullptr;
		if (volume == nullptr) {
			TOAST_WARN("Voxel", "Bucket needs a ProceduralVoxel with voxels");
			return true;
		}

		// walk the third from the near side to the first solid voxel
		glm::ivec3 seed = e.voxel;
		if (e.search && e.search_axis < 3) {
			glm::ivec3 step(0);
			step[e.search_axis] = e.search_step;
			const int32_t limit = static_cast<int32_t>(volume->voxelDims()[e.search_axis]) + 1;
			for (int32_t i = 0; i < limit && !volume->isSolidAt(seed - shape->voxelOrigin()); ++i) {
				seed += step;
			}
		}
		if (!volume->isSolidAt(seed - shape->voxelOrigin())) {
			return true;
		}

		Box<Node> shape_box = shape->box();
		recordHistory(voxelHistory(event::HistoryOperation::create, shape_box, "Bucket fill"), [&] {
			Box<Node> made = requestRuntimeCreate(shape_box, "toast::VoxelBucket");
			if (auto* bucket = made.exists() ? reflect_cast<VoxelBucket>(&*made) : nullptr) {
				bucket->setId(static_cast<int>(std::min(e.id, 255u)));
				bucket->position = (glm::vec3(seed) + 0.5f) * voxel::k_voxel_size;
				bucket->syncTransform();
			}
		});
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});

	m_listener.subscribe<event::VoxelCollapsePieces>([this, active](const auto& e) {
		if (!active()) {
			return false;
		}

		std::vector<VoxelPiece*> pieces;
		std::vector<Box<Node>> boxes;
		ProceduralVoxel* shape = nullptr;
		for (const UID& uid : e.targets) {
			auto target = findFrom(m_root_node, uid);
			VoxelPiece* piece = voxelPieceOf(target);
			ProceduralVoxel* owner = piece != nullptr ? owningProceduralVoxel(*piece) : nullptr;
			if (owner == nullptr || (shape != nullptr && owner != shape)) {
				continue;
			}
			shape = owner;
			pieces.push_back(piece);
			boxes.push_back(target);
		}
		if (shape == nullptr) {
			TOAST_WARN("Voxel", "Bake needs pieces of one ProceduralVoxel");
			return true;
		}

		std::optional<ProceduralVoxel::Composed> composed = shape->composePieces(pieces);
		if (!composed.has_value()) {
			TOAST_WARN("Voxel", "Bake: the pieces draw nothing");
			return true;
		}
		const std::vector<uint8_t> bytes =
		    assets::VoxelModel::capture(composed->volume, shape->paletteUid())->serialize(assets::SaveMode::editor);
		if (!assets::AssetManager::get().saveBytes(e.path, bytes)) {
			TOAST_WARN("Voxel", "Bake could not write {}", e.path);
			return true;
		}
		const std::optional<UID> model_uid = assets::resolveURI(e.path);
		if (!model_uid.has_value()) {
			TOAST_WARN("Voxel", "Bake: {} is not in the asset manifest", e.path);
			return true;
		}

		Box<Node> shape_box = shape->box();
		recordHistory(voxelHistory(event::HistoryOperation::create, shape_box, "Baked"), [&] {
			// The model takes the place of the first piece so the order around it stays the same
			Box<Node> first = boxes.front();
			Box<Node> first_parent = first->parentInternal();
			Box<Node> mesh_box = requestRuntimeCreate(first_parent, "toast::VoxelMesh");
			if (auto* mesh = reflect_cast<VoxelMesh>(&*mesh_box)) {
				mesh->name("Baked");
				mesh->setModel(assets::load<assets::VoxelModel>(*model_uid));
				placePiece(*mesh, {}, composed->origin, composed->origin);
				auto& siblings = first_parent->m_children;
				std::erase(siblings, mesh_box);
				siblings.insert(std::ranges::find(siblings, first), mesh_box);
			}
			for (Box<Node>& box : boxes) {
				std::erase(box->parentInternal()->m_children, box);
				destroyOwnedTree(box);
			}
		});
		event::send<event::RequestHierarchyUpdate>();
		return true;
	});
}

auto Workspace::pickNodeUnderMouse() -> Box<Node> {
	const std::optional<Ray> ray = voxelMouseRay(m_gizmo_mouse_pos);
	if (!ray.has_value() || !m_root_node.exists()) {
		return {};
	}
	const glm::vec3 direction = glm::normalize(ray->direction);

	Box<Node> best;
	float best_t = std::numeric_limits<float>::max();
	const auto consider = [&](const Box<Node>& node, float t) {
		if (t >= 0.0f && t < best_t) {
			best_t = t;
			best = node;
		}
	};

	const auto visit = [&](const auto& self, Box<Node> node) -> void {
		if (!node.exists() || !node->enabled()) {
			return;
		}
		if (auto* voxels = reflect_cast<VoxelNode>(&*node)) {
			if (const voxel::Volume* volume = voxels->volume()) {
				voxels->syncTransform();
				const glm::mat4 to_volume = glm::inverse(voxels->getWorldTransform() * voxels->volumeLocalTransform());
				const glm::vec3 origin = glm::vec3(to_volume * glm::vec4(ray->origin, 1.0f)) / voxel::k_voxel_size;
				const glm::vec3 step = glm::vec3(to_volume * glm::vec4(direction, 0.0f)) / voxel::k_voxel_size;
				// Along step one unit is a world metre, so t compares with the meshes
				if (const auto hit = voxel::raycast(*volume, origin, step, 1e6f)) {
					consider(node, hit->t);
				}
			}
		} else if (auto* mesh = reflect_cast<MeshNode>(&*node); mesh != nullptr && mesh->getMesh().hasValue()) {
			mesh->syncTransform();
			const glm::mat4& world = mesh->getWorldTransform();
			const glm::vec4 local = mesh->getMesh()->boundingSphere();
			const glm::vec3 centre = glm::vec3(world * glm::vec4(glm::vec3(local), 1.0f));
			const float radius =
			    local.w *
			    std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
			const glm::vec3 to_centre = centre - ray->origin;
			const float along = glm::dot(to_centre, direction);
			const float miss = glm::dot(to_centre, to_centre) - (along * along);
			if (miss <= radius * radius) {
				consider(node, std::max(0.0f, along - std::sqrt((radius * radius) - miss)));
			}
		}
		for (const Box<Node>& child : node->children()) {
			self(self, child);
		}
	};
	visit(visit, m_root_node);

	// The hierarchy lists a prefab instance as one node
	while (best.exists() && best->m_prefab_interior) {
		best = best->parent();
	}
	return best;
}

}
