#define GLM_ENABLE_EXPERIMENTAL
#include "ConvexCollider.hpp"
#include "BoxDynamics.hpp"
#include "Physics/PhysicsSystem.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "glm/gtx/fast_square_root.hpp"
#include "glm/gtx/quaternion.hpp"

using namespace glm;
namespace physics {

ConvexCollider::ConvexCollider(const point_list& points, const ColliderData& data) {
	edges.reserve(points.size() - 1);
	vertices.reserve(points.size());

	for (const auto& [point, _] : points) {
		// Add the point to the vertices list
		vertices.emplace_back(point + data.worldPosition);
	}

	std::list<glm::vec2> vertices_list { vertices.begin(), vertices.end() };
	double sign = ShoelaceArea(vertices_list) <= 0 ? 1.0 : -1.0;

	for (const auto& point : vertices) {
		// Compute the edge and add it to its list
		auto it = std::ranges::find(vertices, point);
		auto prev_it = it != vertices.begin() ? std::prev(it) : std::prev(vertices.end());
		vec2 prev = *prev_it;

		dvec2 edge = point - prev;
		// clang-format off
		edges.emplace_back(Line {
				.p1 = prev,
				.p2 = point,
				.normal = sign * normalize(dvec2 { -edge.y, edge.x }),
				.tangent = normalize(edge), .length = length(edge)
		});
		// clang-format on
	}

	friction = data.friction;
	worldPosition = data.worldPosition;
	debugNormals = data.debugNormals;

	PhysicsSystem::AddCollider(this);
}

ConvexCollider::~ConvexCollider() {
	PhysicsSystem::RemoveCollider(this);
}

void ConvexCollider::Debug() {
	for (const auto& e : edges) {
		renderer::DebugLine(e.p1, e.p2, { 1.0f, 0.5f, 0.0f, 1.0f });
		vec2 mp = mix(e.p1, e.p2, 0.5f);
		if (debugNormals) {
			renderer::DebugLine(mp, mp + static_cast<glm::vec2>(e.normal), { 0.0f, 0.5f, 1.0f, 1.0f });
		}
	}
}

auto ConvexRayCollision(Line* ray, ConvexCollider* c) -> std::optional<dvec2> {
	std::optional<dvec2> result = std::nullopt;
	for (Line& l : c->edges) {
		auto cur_dist = LineLineCollision(*ray, l);
		if (cur_dist != std::nullopt && (length2(cur_dist.value() - ray->p1) < length2(result.value() - ray->p1)))
			result = cur_dist;
	}
	return result;
}

}