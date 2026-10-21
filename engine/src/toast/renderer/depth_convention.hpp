/// @file depth_convention.hpp
/// @author dario
/// @date 09/10/2026

#pragma once

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

namespace renderer::depth {

/// Near 1 far 0 for even float precision over distance
inline constexpr bool k_reversed_z = true;

inline constexpr float k_near = k_reversed_z ? 1.0f : 0.0f;
inline constexpr float k_far = k_reversed_z ? 0.0f : 1.0f;

inline constexpr float k_clear = k_far;

inline constexpr vk::CompareOp k_closer = k_reversed_z ? vk::CompareOp::eGreater : vk::CompareOp::eLess;
/// Drawing over a depth prepass and far plane draws such as the skybox
inline constexpr vk::CompareOp k_closer_or_equal = k_reversed_z ? vk::CompareOp::eGreaterOrEqual : vk::CompareOp::eLessOrEqual;

/// Callers still apply their own Y flip
[[nodiscard]]
inline auto perspective(float fovy, float aspect, float near_plane, float far_plane) -> glm::mat4 {
	// Swapping the planes maps near to 1 and far to 0
	return k_reversed_z ? glm::perspectiveRH_ZO(fovy, aspect, far_plane, near_plane)
	                    : glm::perspectiveRH_ZO(fovy, aspect, near_plane, far_plane);
}

}
