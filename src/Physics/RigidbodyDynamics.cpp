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
	renderer::DebugLine(m.contact1, m.contact1 + (m.normal * m.depth), {1.0f, 0.0f, 1.0f, 1.0f});
	if (m.contactCount == 2) {
		renderer::DebugCircle(m.contact2, 0.1, { 0.0f, 1.0f, 0.0f, 1.0f });
		renderer::DebugLine(m.contact2, m.contact2 + (m.normal * m.depth), {1.0f, 0.0f, 1.0f, 1.0f});
	}
}

void RbKinematics(Rigidbody* rb) {
	auto& velocity = rb->velocity;

	// Avoid a Rigidbody having 0 mass
	if (rb->mass < 0.1) {
		rb->mass = 1;
	}

	// Sum forces
	glm::dvec2 forces_sum = std::ranges::fold_left(rb->forces, glm::dvec2(0.0), std::plus {});
	glm::dvec2 accel = (forces_sum / rb->mass) + (PhysicsSystem::gravity() * dvec2{rb->gravityScale});

	// Integrate velocity
	velocity += accel * Time::fixed_delta();

	// Apply drag
	const double damping = std::exp(-rb->linearDrag * Time::fixed_delta());
	velocity *= damping;
}

void RbIntegration(Rigidbody* rb) {
	// Integrate position
	dvec2 pos = rb->GetPosition();
	pos += rb->velocity * Time::fixed_delta();
	rb->SetPosition(pos);
}

void RbResetVelocity(Rigidbody* rb) {
	// Set the velocity to 0 at the start of the simulation
	rb->velocity = { 0.0, 0.0 };
}

auto RbRbCollision(Rigidbody* rb1, Rigidbody* rb2) -> std::optional<Manifold> {
	dvec2 pos1 = rb1->GetPosition();
	dvec2 pos2 = rb2->GetPosition();

	double penetration = (rb1->radius + rb2->radius) - distance(pos1, pos2);

	// if penetration is negative we don't have a collision
	if (penetration <= 0.0) {
		return std::nullopt;
	}

	auto manifold = Manifold {
		.normal = normalize(pos1 - pos2),
		.depth = penetration
	};

	// tangent direction perpendicular to normal
	dvec2 base_point = pos2 + manifold.normal * (rb2->radius - manifold.depth);
	manifold.contact1 = base_point;
	manifold.contact2 = base_point;
	manifold.contactCount = 1;

	DebugManifold(manifold);
	return manifold;
}

void RbRbResolution(Rigidbody* rb1, Rigidbody* rb2, Manifold manifold) {

}

auto RbMeshCollision(Rigidbody* rb, ConvexCollider* c) -> std::optional<Manifold> {
	dvec2 rb_pos = rb->GetPosition();

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

	// compute contact information for the chosen manifold
	// distance from circle center to the supporting plane along normal
	const double dist_to_plane = std::max(0.0, rb->radius - best.depth);
	// tangent direction perpendicular to normal
	dvec2 tangent = normalize(dvec2 { -best.normal.y, best.normal.x });
	// half-length of the chord of intersection on the circle
	double chord_half = std::sqrt(std::max(0.0, (rb->radius * rb->radius) - (dist_to_plane * dist_to_plane)));
	// base point on circle center shifted toward contact plane
	dvec2 base_point = rb_pos - best.normal * dist_to_plane;

	if (chord_half <= PhysicsSystem::eps()) {
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
	dvec2 velocity = rb->velocity;
	dvec2 position = rb->GetPosition();

	// Early out if body is effectively infinite mass
	if (rb->mass <= 0.0) return;

	double inv_mass = 1.0 / rb->mass;

	dvec2 contact_tangent = { -manifold.normal.y, manifold.normal.x };

	// unfold velocity in normal and tangencial
	double normal_speed = dot(velocity, manifold.normal);
	double tangent_speed = dot(velocity, contact_tangent);
	// only apply restitution if we're moving faster than the restitution threshold
	double restitution = (std::abs(normal_speed) < rb->restitutionThreshold) ? 0.0 : rb->restitution;

	// only resolve if we're going towards the object
	if (normal_speed < 0.0) {
		// Normal impulse (bounce response)
		// Jn = -(1 + e) * vn / invMass = -(1 + e) * vn * mass
		double normal_impulse = -(1.0 + restitution) * normal_speed / inv_mass;

		// Coulomb friction
		double max_friction_impulse = c->friction * std::abs(normal_impulse);

		// calculate tangential impulse to cancel tangential speed
		double tangential_impulse = -tangent_speed / inv_mass;
		tangential_impulse = clamp(tangential_impulse, -max_friction_impulse, max_friction_impulse);

		// Apply impulses to velocity
		velocity += (normal_impulse * inv_mass) * manifold.normal + (tangential_impulse * inv_mass) * contact_tangent;
	}

	// positional correction
	double penetration_correction = std::max(manifold.depth - PhysicsSystem::pos_slop(), 0.0) * PhysicsSystem::pos_ptc();
	position += penetration_correction * manifold.normal;

	// velocity correction
	double residual_normal_speed = glm::dot(velocity, manifold.normal);
	if (residual_normal_speed < 0.0) {
		velocity -= residual_normal_speed * manifold.normal;
	}
	rb->velocity = velocity;

	rb->SetPosition(position);
}

}
