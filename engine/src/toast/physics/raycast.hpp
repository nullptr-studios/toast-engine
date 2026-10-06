/**
 * @file raycast.hpp
 * @author Dante Harper
 * @date 25 Sep 26
 *
 * @brief Raycast Implmentation mostly from https://iquilezles.org/articles/intersectors/ and
 * https://www.realtimerendering.com/intersections.html
 */

#pragma once

#include "toast/world/box.hpp"

#include <glm/glm.hpp>

namespace physics {

struct RayHit {
	toast::Box<toast::Node> node;
	glm::vec3 position;
	glm::vec3 normal;
	float distance;
};

TOAST_API auto raycast(glm::vec3 pos, glm::vec3 dir, float max_distance = -1.0f, int max_targets = 3) -> std::vector<RayHit>;

struct SphereHit {
	toast::Box<toast::Node> node;
	glm::vec3 position;
	glm::vec3 normal;
	float penetration;
};

TOAST_API auto sphereOverlap(glm::vec3 position, float radius) -> std::optional<SphereHit>;


}
