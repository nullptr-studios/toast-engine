#include "query_snapshot.hpp"
#include "raycast.hpp"
#include "toast/physics/shape_query.hpp"
#include "toast/physics/simulator.hpp"

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

namespace physics {

auto sphereOverlap(glm::vec3 position, float radius, std::optional<LayerFilter> layers) -> std::optional<SphereHit> {
	ZoneScoped;
	const auto snapshot = Simulator::querySnapshot();
	if (!snapshot || !std::isfinite(radius) || radius <= 0.0f || !std::isfinite(position.x) || !std::isfinite(position.y) ||
	    !std::isfinite(position.z)) {
		return std::nullopt;
	}
	return snapshotSphereOverlap(*snapshot, position, radius, layers);
}

auto overlapAABB(glm::vec3 min, glm::vec3 max) -> bool {
	const auto snapshot = Simulator::querySnapshot();
	return snapshot && snapshotAABBOverlap(*snapshot, AABB {.min = min, .max = max});
}

}
