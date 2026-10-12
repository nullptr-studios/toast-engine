/**
 * @file raycast.hpp
 * @author Dante Harper
 * @date 25 Sep 26
 *
 * @brief Raycast Implmentation mostly from https://iquilezles.org/articles/intersectors/ and
 * https://www.realtimerendering.com/intersections.html
 */

#pragma once

#include "body.hpp"
#include "shape.hpp"
#include "toast/world/box.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace physics {

struct RayHit {
	toast::Box<toast::Node> node;
	glm::vec3 position;
	glm::vec3 normal;
	float distance;
	BodyID body;
	ShapeID shape;
};

/**
 * The nearest thing a ray hits
 * @param ignored Shapes of this body are not hit
 */
TOAST_API auto raycast(glm::vec3 origin, glm::vec3 direction, float max_distance = -1.0f, BodyID ignored = {})
    -> std::optional<RayHit>;

/**
 * Up to @p max_hits things a ray hits
 * 
 * Adds a hit per time a ray enters a body
 */
TOAST_API auto
    raymulticast(glm::vec3 origin, glm::vec3 direction, float max_distance = -1.0f, int max_hits = -1, BodyID ignored = {})
        -> std::vector<RayHit>;

/**
 * Like @c raymulticast from @p from until @p to
 */
TOAST_API auto linecast(glm::vec3 from, glm::vec3 to, int max_hits = -1, BodyID ignored = {}) -> std::vector<RayHit>;

struct SphereHit {
	toast::Box<toast::Node> node;
	glm::vec3 position;
	glm::vec3 normal;
	float penetration;
};

TOAST_API auto sphereOverlap(glm::vec3 position, float radius) -> std::optional<SphereHit>;

/// Tests a world-space axis-aligned box against enabled physics shapes.
TOAST_API auto overlapAABB(glm::vec3 min, glm::vec3 max) -> bool;

}
