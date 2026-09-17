/**
 * @file gizmo_layout.hpp
 * @brief Geometry + hit-test constants shared between DebugPass (renders the gizmos) and Workspace
 *        (hit-tests/drags them)
 *
 * All lengths are in gizmo-local unit space, the actual world-space size is this multiplied by the
 * distance-based uniform scale computed each frame (k_screen_size * distance-to-camera), so the gizmo
 * keeps a constant apparent size on screen regardless of distance
 */

#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <utility>

namespace toast {

/// @brief Mirrors editor.WorkspaceViewModel.GizmoTool / proto SetGizmoTool - 0 Select, 1 Translate,
/// 2 Rotate, 3 Scale, 4 Ruler
enum class GizmoTool : uint8_t {
	select,
	translate,
	rotate,
	scale,
	ruler
};

/// @brief Which part of the active gizmo a ray/click is interacting with, axis_x/y/z and center are
/// reused across Translate/Rotate/Scale (only one tool is ever active); plane_xy/yz/xz are Translate-only
enum class GizmoHandle : int8_t {
	none = -1,
	axis_x = 0,
	axis_y = 1,
	axis_z = 2,
	plane_xy = 3,
	plane_yz = 4,
	plane_xz = 5,
	center = 6,
};

namespace gizmo_layout {

constexpr float k_screen_size = 0.15f;    ///< world-units of gizmo size per world-unit of camera distance

// Translate: arrow shaft + pyramid head per axis, small plane quads, center free-move handle
constexpr float k_shaft_length = 0.8f;
constexpr float k_shaft_half_size = 0.02f;
constexpr float k_head_length = 0.25f;
constexpr float k_head_half_size = 0.06f;

/// @brief Plane-handle quads sit near the origin, offset along each of the plane two axes
constexpr float k_plane_offset = 0.25f;
constexpr float k_plane_size = 0.2f;
constexpr float k_center_half_size = 0.06f;

/// @brief Hit tolerances, in the same gizmo-local unit space as the geometry above
constexpr float k_axis_hit_radius = 0.05f;
constexpr float k_center_hit_radius = 0.12f;

// Rotate: one flat ring per axis, lying in the plane perpendicular to that axis
constexpr float k_ring_radius = 0.9f;
constexpr float k_ring_thickness = 0.05f;    ///< band width, used for both rendering and hit-testing
constexpr int k_ring_segments = 48;

// Scale: same shaft as translate, but a cube head instead of a pyramid, plus a center cube for uniform scale
constexpr float k_scale_head_half_size = 0.07f;

/// @brief Deterministic in-plane basis (u,v) perpendicular to axis
inline auto ringBasis(const glm::vec3& axis) -> std::pair<glm::vec3, glm::vec3> {
	const glm::vec3 reference = std::abs(glm::dot(axis, glm::vec3(0, 0, 1))) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
	const glm::vec3 u = glm::normalize(glm::cross(axis, reference));
	const glm::vec3 v = glm::cross(axis, u);
	return {u, v};
}

}
}
