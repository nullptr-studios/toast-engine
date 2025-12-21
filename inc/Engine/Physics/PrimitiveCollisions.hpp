/**
 * @file PrimitiveColiisions.hpp
 * @author Iñaki
 * @date 30/09/25
 */

#pragma once
#include "Engine/Physics/RayCast.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"

#include <glm/glm.hpp>
#include <optional>

namespace physics {
enum class ColliderFlags : unsigned char;
class ICollider;

/** @class physics::ContactInfo
 *  @brief class that handles collision information
 */
struct ContactInfo {
	glm::vec2 normal;
	glm::vec2 intersection;
	double penetration;
};

/**
 *
 * @param point
 * @param circle_center
 * @param radius
 * @return bool
 */
bool PointInCircle(glm::vec2 point, glm::vec2 circle_center, float radius);

/**
 *
 * @param point
 * @param position_1
 * @param scale_1
 * @param angle_1
 * @return bool
 */
bool PointInObb(glm::vec2 point, glm::vec2 position_1, glm::vec2 scale_1, float angle_1);

/**
 *
 * @param point
 * @param position_1
 * @param scale_1
 * @return
 */
bool PointInAbb(glm::vec2 point, glm::vec2 position_1, glm::vec2 scale_1);

/**
 * @brief checking if two circles collide
 * @param position_1 center for the first circle
 * @param radius_1 	 radius for the first circle
 * @param position_2 center for the second circle
 * @param radius_2 	 radius for the first circle
 * @return std::optional<Contact>
 */
std::optional<ContactInfo> CircleToCircle(glm::vec2 position_1, float radius_1, glm::vec2 position_2, float radius_2);

/**
 * @brief checking if two obbs collide
 * @param position_1 center for the first obb
 * @param scale_1 	 scale for the first obb
 * @param angle_1 	 rotation for the first obb
 * @param position_2 center for the second obb
 * @param scale_2    scale for the second obb
 * @param angle_2 	 rotation for the second obb
 * @return std::optional<Contact>
 */
std::optional<ContactInfo> ObbToObb(glm::vec2 position_1, glm::vec2 scale_1, float angle_1, glm::vec2 position_2, glm::vec2 scale_2, float angle_2);

/**
 *
 * @param point
 * @param direction
 * @param center
 * @param radius
 * @param limit
 */
std::optional<RayCastResult> RayToCircle(glm::vec2 point, glm::vec2 direction, glm::vec2 center, float radius);

/**
 * @param point
 * @param direction
 * @param position
 * @param scale
 * @param angle
 * @param limit
 */
std::optional<RayCastResult> RayToObb(glm::vec2 point, glm::vec2 direction, glm::vec2 position, glm::vec2 scale, float angle, float limit);

/**
 * @brief Handles the collision between two colliders
 * @param collider1
 * @param collider2
 */
std::optional<ContactInfo> Collide(ICollider* collider1, ICollider* collider2);

/**
 * @brief Handles the raycast collision
 * @param position
 * @param direction
 * @param collider
 */
std::optional<RayCastResult> Collide(glm::vec2 position, glm::vec2 direction, ICollider* collider, ColliderFlags rayFlag, float limit);

}
