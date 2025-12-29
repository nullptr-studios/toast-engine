#include "RigidbodyDynamics.hpp"

#include "ConvexCollider.hpp"
#include "PhysicsSystem.hpp"

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Profiler.hpp>
#include <Engine/Core/Time.hpp>
#include <Engine/Physics/Rigidbody.hpp>
#include <Engine/Renderer/DebugDrawLayer.hpp>
#include <glm/glm.hpp>

namespace physics {
using namespace glm;

static void DebugManifold(const Manifold& m) {
	renderer::DebugCircle(m.contact1, 0.1, { 0.0f, 1.0f, 0.0f, 1.0f });
	renderer::DebugLine(m.contact1, m.contact1 + (m.normal * m.depth));
	if (m.contactCount == 2) {
		renderer::DebugCircle(m.contact2, 0.1, { 0.0f, 1.0f, 0.0f, 1.0f });
		renderer::DebugLine(m.contact2, m.contact2 + (m.normal * m.depth));
	}
}

void RbKinematics(Rigidbody* rb) {
	dvec2 velocity = rb->velocity;
	dvec2 position = rb->GetPosition();

	// Deal forces
	dvec2 forces_sum = std::ranges::fold_left(rb->forces, glm::dvec2(0.0f), std::plus {});
	dvec2 accel_sum = forces_sum / rb->mass;
	accel_sum += PhysicsSystem::gravity() * rb->gravityScale;

	// Integrate
	velocity += accel_sum * Time::fixed_delta();
	velocity *= (1 - rb->linearDrag);
	position += velocity * Time::fixed_delta();

	rb->SetPosition(position);
}

auto RbRbCollision(Rigidbody* rb1, Rigidbody* rb2) -> std::optional<Manifold> {
	dvec2 pos1 = rb1->GetPosition();
	dvec2 pos2 = rb2->GetPosition();

	double penetration = (rb1->radius + rb2->radius) - distance(pos1, pos2);

	// if penetration is negative we don't have a collision
	if (penetration <= 0.0) {
		return std::nullopt;
	}

	return {};
	// return {
	// 	.penetration = penetration,
	// 	.normal = normalize(pos2 - pos1)
	// };
}

auto RbMeshCollision(Rigidbody* rb, ConvexCollider* c) -> std::optional<Manifold> {
	dvec2 rb_pos = rb->GetPosition();
	renderer::DebugCircle(rb_pos, rb->radius);

	std::list<Manifold> manifolds;

	// This method will use the SAT algorithm
	for (const auto& edge : c->edges) {
		// project all points onto the edge normal and keep the minimum & maximum
		double min_proj = dot(dvec2 { c->vertices[0] }, edge.normal);
		double max_proj = min_proj;

		for (int i = 1; i < c->vertices.size(); i++) {
			double v = dot(dvec2 { c->vertices[i] }, edge.normal);
			min_proj = std::min(v, min_proj);
			max_proj = std::max(v, max_proj);
		}

		// project rb position onto edge normal and then add radius
		double rb_proj = dot(rb_pos, edge.normal);
		double rb_max_proj = rb_proj + rb->radius;
		double rb_min_proj = rb_proj - rb->radius;

		// if theres no collision on the projection, there is no collision
		if (rb_max_proj < min_proj || rb_min_proj > max_proj) {
			return std::nullopt;
		}

		// compute overlap along the axis to store as penetration depth candidate
		const double overlap = std::min(max_proj - rb_min_proj, rb_max_proj - min_proj);

		// if there is we will generate a manifold to compare later on
		// clang-format off
		manifolds.emplace_back(Manifold {
			.normal = edge.normal,
			.contact1 = {0.0f, 0.0f},
			.contact2 = {0.0f, 0.0f},
			.contactCount = -1,
			.depth = overlap
		});
		// clang-format on
	}

	// select the axis with the least penetration depth
	auto it = std::ranges::min_element(manifolds, {}, &Manifold::depth);
	if (it == manifolds.end()) {
		return std::nullopt;
	}
	auto best = *it;
	constexpr double EPS = 1e-6;

	// compute contact information for the chosen manifold
	// distance from circle center to the supporting plane along normal
	const double dist_to_plane = std::max(0.0, rb->radius - best.depth);
	// tangent direction perpendicular to normal
	dvec2 tangent = normalize(dvec2 { -best.normal.y, best.normal.x });
	// half-length of the chord of intersection on the circle
	double chord_half = std::sqrt(std::max(0.0, (rb->radius * rb->radius) - (dist_to_plane * dist_to_plane)));
	// base point on circle center shifted toward contact plane
	dvec2 base_point = rb_pos - best.normal * dist_to_plane;

	if (chord_half <= EPS) {
		// if overlap is almost zero, use a single contact
		best.contact1 = base_point;
		best.contact2 = base_point;
		best.contactCount = 1;
	} else {
		// otherwise use two symmetric contact points on the chord
		best.contact1 = base_point - tangent * chord_half;
		best.contact2 = base_point + tangent * chord_half;
		best.contactCount = 2;
	}

	DebugManifold(best);
	return best;
}

void RbMeshResolution(Rigidbody* rb, ConvexCollider* c, Manifold manifold) { }

}
