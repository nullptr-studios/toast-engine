/**
 * @file volume_bounds.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "voxel_constants.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace voxel {

[[nodiscard]]
inline auto localExtent(glm::uvec3 brick_dims) noexcept -> glm::vec3 {
	return glm::vec3(brick_dims) * k_brick_size;
}

/// @returns centre in xyz and radius in w
[[nodiscard]]
inline auto worldBoundingSphere(const glm::mat4& model, glm::uvec3 brick_dims) -> glm::vec4 {
	const glm::vec3 extent = localExtent(brick_dims);
	const glm::vec3 centre = glm::vec3(model * glm::vec4(extent * 0.5f, 1.0f));

	float radius_squared = 0.0f;
	for (int corner = 0; corner < 8; ++corner) {
		const glm::vec3 local(
		    (corner & 1) != 0 ? extent.x : 0.0f, (corner & 2) != 0 ? extent.y : 0.0f, (corner & 4) != 0 ? extent.z : 0.0f
		);
		const glm::vec3 offset = glm::vec3(model * glm::vec4(local, 1.0f)) - centre;
		radius_squared = std::max(radius_squared, glm::dot(offset, offset));
	}
	return glm::vec4(centre, std::sqrt(radius_squared));
}

/// @brief Pass the near plane distance as @p margin
[[nodiscard]]
inline auto containsPoint(const glm::mat4& inverse_model, glm::uvec3 brick_dims, glm::vec3 world_point, float margin = 0.0f)
    -> bool {
	const glm::vec3 local = glm::vec3(inverse_model * glm::vec4(world_point, 1.0f));
	const float largest_scale = std::max(
	    {glm::length(glm::vec3(inverse_model[0])),
			 glm::length(glm::vec3(inverse_model[1])),
			 glm::length(glm::vec3(inverse_model[2]))}
	);
	const glm::vec3 guard(margin * largest_scale);
	return glm::all(glm::greaterThanEqual(local, -guard)) && glm::all(glm::lessThanEqual(local, localExtent(brick_dims) + guard));
}

}
