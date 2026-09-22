/// @file light_culling.hpp
/// @author dario
/// @date 21/09/2026

#pragma once

#include <glm/glm.hpp>

namespace renderer::light_culling {

struct Sphere {
	glm::vec3 center {0.0f};
	float radius = 0.0f;
};

struct DepthRange {
	float near_depth = 0.0f;
	float far_depth = 0.0f;
};

/// Smallest sphere around the cone of a spot light capped at its range
[[nodiscard]]
auto spotBounds(const glm::vec3& position, const glm::vec3& direction, float range, float outer_radians) -> Sphere;

/// Only ranks lights against each other
[[nodiscard]]
auto importance(const glm::vec3& color, float intensity, float range, float camera_distance, float cone_fraction) -> float;

/// Ends at the farthest visible light so slices are not spent on empty depth
[[nodiscard]]
auto clusterDepthRange(float camera_near, float camera_far, float light_reach) -> DepthRange;

}
