#include "../../inc/Engine/Physics/RayCast.hpp"

#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "PhysicsSystem.hpp"

using namespace glm;

namespace physics {

std::optional<RayCastResult> RayCast(vec2 point, vec2 direction, ColliderFlags flags, bool debug, float limit) {
	std::optional<RayCastResult> result = std::nullopt;
	std::optional<RayCastResult> min_result = std::nullopt;

	for (auto* collider : PhysicsSystem::GetInstance()->GetColliders()) {
		result = Collide(point, direction, collider, flags, limit);
		if (result != std::nullopt) {
			if (min_result == std::nullopt) {
				min_result = result;
			}

			if (length2(result->position - point) < length2(min_result->position - point)) {
				min_result = result;
			}
		}
	}

	if (debug) {
		renderer::DebugLine(point, point + vec2(100.0f, 100.0f) * direction, vec4(1.0f, 0.0f, 0.0f, 0.0f));
	}

	if (debug && min_result != std::nullopt) {
		renderer::DebugLine(point, min_result->position, vec4(0.0f, 0.0f, 1.0f, 1.0f));
	}
	return min_result;
}

}
