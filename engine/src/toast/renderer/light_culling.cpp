/// @file light_culling.cpp
/// @author dario
/// @date 21/09/2026

#include "light_culling.hpp"

#include "clustered_lighting_constants.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace renderer::light_culling {

auto spotBounds(const glm::vec3& position, const glm::vec3& direction, float range, float outer_radians) -> Sphere {
	const float half_angle = std::clamp(outer_radians, 0.0f, glm::half_pi<float>());
	const float cos_angle = std::cos(half_angle);

	if (cos_angle >= glm::one_over_root_two<float>()) {
		const float radius = range / (2.0f * cos_angle);
		return {position + (direction * radius), radius};
	}
	return {position + (direction * (range * cos_angle)), range * std::sin(half_angle)};
}

auto importance(const glm::vec3& color, float intensity, float range, float camera_distance, float cone_fraction) -> float {
	constexpr glm::vec3 k_luminance {0.2126f, 0.7152f, 0.0722f};

	const float reach_squared = range * range;
	const float brightness = glm::dot(color, k_luminance) * std::max(intensity, 0.0f);
	const float value =
	    brightness * cone_fraction * reach_squared / ((camera_distance * camera_distance) + reach_squared + 1.0e-6f);
	return std::isnan(value) ? 0.0f : value;
}

auto clusterDepthRange(float camera_near, float camera_far, float light_reach) -> DepthRange {
	using namespace clustered_lighting;

	const float near_depth = std::max(camera_near, k_min_cluster_near);
	const float min_far = near_depth * k_min_cluster_depth_ratio;
	return {near_depth, std::clamp(light_reach, min_far, std::max(camera_far, min_far))};
}

}
