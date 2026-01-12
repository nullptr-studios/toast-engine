/**
 * @file Raycast.hpp
 * @author Iñaki
 * @date 12/01/2026
 *
 */
#pragma once
#include "glm/vec2.hpp"
#include "src/Physics/ConvexCollider.hpp"

namespace physics {
class ConvexCollider;
std::optional<ConvexCollider> RayCast(glm::vec2 point, glm::vec2 dir);
}
