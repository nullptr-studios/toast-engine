#include "Toast/Physics/Raycast.hpp"

#include "PhysicsSystem.hpp"
#include "Toast/Physics/Line.hpp"

namespace physics {
using namespace glm;

std::optional<RayResult> RayCast(const vec2 point, const vec2 dir, ColliderFlags flags) {
	// Construct line to send to collision
	const vec2 direction = normalize(dir);
	const vec2 normal = { -direction.y, direction.x };
	Line ray = {
		.p1 = point,
		.p2 = point + normalize(dir) * std::numeric_limits<float>::max(),
		.normal = normal,
		.tangent = direction
	};

	return PhysicsSystem::RayCollision(&ray, flags);
}

}
