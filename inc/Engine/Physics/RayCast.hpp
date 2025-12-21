/**
 * @file RayCast.hpp
 * @author Iñaki
 * @date 27/10/2025
 *
 * @brief [TODO: Brief description of the files purpose]
 */

#pragma once
#include "glm/vec2.hpp"

namespace physics {

enum class ColliderFlags : unsigned char;

class ICollider;

struct RayCastResult {
	glm::vec2 position;
	glm::vec2 direction;
	ICollider* collider;
};

/**
 * @param point	point of origin
 * @param direction direction of the ray
 * @param debug parameter that draws a debug line if true
 * @param limit range limit for the raycast
 * @return std::optional<RayCastResult>
 */
std::optional<RayCastResult> RayCast(glm::vec2 point, glm::vec2 direction, ColliderFlags flags, bool debug = false, float limit = 1000.f);

}
