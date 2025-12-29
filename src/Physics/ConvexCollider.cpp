#include "ConvexCollider.hpp"

#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "Physics/PhysicsSystem.hpp"

using namespace physics;
using namespace glm;

ConvexCollider::ConvexCollider(const point_list& points) {
	edges.reserve(points.size() - 1);
	vertices.reserve(points.size());

	for (const auto& [point, _] : points) {
		// Add the point to the vertices list
		vertices.emplace_back(point);

		// Compute the edge and add it to its list
		auto it = std::ranges::find(points, std::pair { point, _ });
		auto prev_it = it != points.begin() ? std::prev(it) : std::prev(points.end());
		vec2 prev = prev_it->first;

		dvec2 edge = point - prev;
		edges.emplace_back(
		    Line { .p1 = prev, .p2 = point, .normal = normalize(dvec2 { -edge.y, edge.x }), .tangent = normalize(edge), .length = length(edge) }
		);
	}

	PhysicsSystem::AddCollider(this);
}

ConvexCollider::~ConvexCollider() {
	PhysicsSystem::RemoveCollider(this);
}

void ConvexCollider::Debug() {
	for (const auto& e : edges) {
		renderer::DebugLine(e.p1, e.p2, { 1.0f, 0.5f, 0.0f, 1.0f });
		vec2 mp = mix(e.p1, e.p2, 0.5f);
		renderer::DebugLine(mp, mp + static_cast<glm::vec2>(e.normal), { 0.0f, 0.5f, 1.0f, 1.0f });
	}
}
