#include "raycast.hpp"
#include "toast/physics/shape_query.hpp"
#include "toast/physics/simulator.hpp"

#include <algorithm>
#include <cmath>

namespace physics {

auto sphereOverlap(glm::vec3 position, float radius) -> std::optional<SphereHit> {
	Simulator* simulator = Simulator::current();
	if (simulator == nullptr || !std::isfinite(radius) || radius <= 0.0f || !std::isfinite(position.x) ||
	    !std::isfinite(position.y) || !std::isfinite(position.z)) {
		return std::nullopt;
	}

	std::vector<QueryContact> contacts;
	if (!simulator->overlapSphere(SphereShape {.radius = radius}, position, 0.0f, contacts)) {
		return std::nullopt;
	}

	const QueryContact& deepest = *std::ranges::max_element(contacts, {}, &QueryContact::penetration);
	return SphereHit {
	  .node = Simulator::nodeFor(deepest.body),
	  .position = deepest.point,
	  .normal = deepest.normal,
	  .penetration = deepest.penetration,
	};
}

auto overlapAABB(glm::vec3 min, glm::vec3 max) -> bool {
	Simulator* simulator = Simulator::current();

	return simulator->overlapAABB(AABB {.min = min, .max = max});
}

}
