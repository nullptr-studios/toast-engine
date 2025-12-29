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

	// Deal forces
	dvec2 forces_sum = std::ranges::fold_left(rb->forces, glm::dvec2(0.0f), std::plus {});
	dvec2 accel_sum = forces_sum / rb->mass;
	accel_sum += PhysicsSystem::gravity() * rb->gravityScale;

	// Integrate
	velocity += accel_sum * Time::fixed_delta();
	velocity *= (1 - rb->linearDrag);
}

void RbIntegration(Rigidbody* rb) {
	dvec2 pos = rb->GetPosition();
	pos += rb->velocity * Time::fixed_delta();
	rb->SetPosition(pos);
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

void RbMeshResolution(Rigidbody* rb, ConvexCollider* c, Manifold manifold) {
	// Current linear velocity and position of the rigidbody
	dvec2 current_velocity = rb->velocity;
	dvec2 current_position = rb->GetPosition();

	// Early out if body is effectively infinite mass
	if (rb->mass <= 0.0) {
		// integrate using the pre-step velocity; swap to corrected velocity if desired
		current_position += current_velocity * Time::fixed_delta();
		rb->SetPosition(current_position);
		return;
	}

	double inv_mass = 1.0 / rb->mass;

	// Contact basis: tangent is perpendicular to normal
	dvec2 contact_tangent = { -manifold.normal.y, manifold.normal.x };

	// unfold velocity in normal and tangencial
	double normal_speed = dot(current_velocity, manifold.normal);
	double tangent_speed = dot(current_velocity, contact_tangent);
	// only apply restitution if we're moving faster than the restitution threshold
	double contact_restitution = (std::abs(normal_speed) < rb->restitutionThreshold) ? 0.0 : rb->restitution;

	// only resolve if we're going towards the object
	if (normal_speed < 0.0) {
		// Normal impulse (bounce response), single dynamic body vs static collider:
		// Jn = -(1 + e) * vn / invMass = -(1 + e) * vn * mass
		double normal_impulse = -(1.0 + contact_restitution) * normal_speed / inv_mass;

		// Coulomb friction
		double friction_coefficient = c->friction;
		double max_friction_impulse = friction_coefficient * std::abs(normal_impulse);

		// calculate tangential impulse to cancel tangential speed
		double tangential_impulse = -tangent_speed / inv_mass;
		tangential_impulse = clamp(tangential_impulse, -max_friction_impulse, max_friction_impulse);

		// Apply impulses to velocity
		rb->velocity += (normal_impulse * inv_mass) * manifold.normal + (tangential_impulse * inv_mass) * contact_tangent;
	}

	// positional correction
	constexpr double POS_CORRECT_SLOP = 1e-3;
	constexpr double POS_CORRECT_PCT = 0.3;
	double penetration_correction = std::max(manifold.depth - POS_CORRECT_SLOP, 0.0) * POS_CORRECT_PCT;
	current_position += penetration_correction * manifold.normal;

	// velocity correction
	double residual_normal_speed = glm::dot(rb->velocity, manifold.normal);
	if (residual_normal_speed < 0.0) {
		current_velocity -= residual_normal_speed * manifold.normal;
	}
	rb->velocity = current_velocity;

	rb->SetPosition(current_position);
}

}
