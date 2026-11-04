/**
 * @file light_gizmo.hpp
 * @author dario
 * @date 29/10/2026
 * @brief Selected light shapes and the viewport handles that edit them, laid out like the collider size handles
 */

#pragma once

#include "gizmo_layout.hpp"
#include "light.hpp"

#include <array>
#include <glm/glm.hpp>
#include <utility>

namespace toast::light_gizmo {

constexpr size_t k_max_handles = 6;

struct Handle {
	GizmoHandle handle = GizmoHandle::none;
	/// Light local space where x is right, y forward and z up
	glm::vec3 local {0.0f};
};

[[nodiscard]]
auto handles(const Light& light) -> std::pair<std::array<Handle, k_max_handles>, uint32_t>;

/// Where the handle sits along its own axis, always positive
[[nodiscard]]
auto handleDistance(const Light& light, GizmoHandle handle) -> float;

void applyHandleDistance(Light& light, GizmoHandle handle, float distance);

/// Reflected name of the field a handle edits or null
[[nodiscard]]
auto handleField(const Light& light, GizmoHandle handle) -> const char*;

void draw(const Light& light);

}
