#include "Toast/Physics/Raycast.hpp"

#include "PhysicsSystem.hpp"
#include "Toast/Physics/Line.hpp"

namespace physics {
using namespace glm;
std::optional<ConvexCollider> RayCast(vec2 point, vec2 dir) {
	Line ray;
	ray.p1 = point;
	ray.p2 = point + dir * std::numeric_limits<float>::max();
	return PhysicsSystem::RayCollision(&ray);
}
}
