/**
 * @file ray_march.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "gpu_layout.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <toast/export.hpp>

namespace voxel {

inline constexpr uint32_t k_max_march_steps = 1024;

/// @brief Relative to t
inline constexpr float k_march_tie_epsilon = 1e-6f;

struct RayHit {
	bool hit = false;

	/// t_min when the ray started inside a voxel
	float t = 0.0f;

	glm::ivec3 voxel {0};

	/// -1 when the ray started inside a solid voxel
	int32_t axis = -1;

	uint8_t material = k_empty_palette_index;

	uint32_t steps = 0;
};

/// @brief CPU twin of voxel_dda.slang marchRay so change both together
/// @p direction need not be normalised and zero components are allowed
[[nodiscard]]
TOAST_API auto marchRay(
    const gpu::PackedPool& pool, const gpu::PackedScene& scene, uint32_t volume, glm::vec3 origin, glm::vec3 direction,
    float t_min, float t_max, uint32_t max_steps = k_max_march_steps
) -> RayHit;

}
