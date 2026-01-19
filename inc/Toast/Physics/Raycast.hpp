/**
 * @file Raycast.hpp
 * @author Iñaki
 * @date 12/01/2026
 *
 */
#pragma once
#include "../src/Physics/ConvexCollider.hpp"
#include "Rigidbody.hpp"
#include "Toast/Objects/Actor.hpp"
#include "glm/vec2.hpp"

namespace physics {
class ConvexCollider;
struct RayResult {
	ConvexCollider* collider = nullptr;
	Rigidbody* rigid = nullptr;

	//if false collider, otherwise rigidbody
	bool colOrRb = false;;
};

std::optional<RayResult> RayCast(glm::vec2 point, glm::vec2 dir);

}
