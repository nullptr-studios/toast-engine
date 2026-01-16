#define GLM_ENABLE_EXPERIMENTAL
#include "RigidbodyDynamics.hpp"
#include "ConvexCollider.hpp"
#include "PhysicsSystem.hpp"
#include "Toast/Log.hpp"
#include "Toast/Physics/Rigidbody.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Time.hpp"
#include "glm/gtx/norm.hpp"

#include <glm/glm.hpp>


namespace physics {
using namespace glm;

void Manifold::Debug() const {
	renderer::DebugCircle(contact1, 0.1, { 0.0f, 1.0f, 0.0f, 1.0f });
	renderer::DebugLine(contact1, contact1 + (normal * depth), { 1.0f, 0.0f, 1.0f, 1.0f });
	if (contactCount == 2) {
		renderer::DebugCircle(contact2, 0.1, { 0.0f, 1.0f, 0.0f, 1.0f });
		renderer::DebugLine(contact2, contact2 + (normal * depth), { 1.0f, 0.0f, 1.0f, 1.0f });
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
	glm::dvec2 accel = (forces_sum / rb->mass) + (PhysicsSystem::gravity() * dvec2 { rb->gravityScale });

	// Integrate velocity
	velocity += accel * Time::fixed_delta();

	// Apply drag
	const double damping = std::exp(-rb->linearDrag * Time::fixed_delta());
	velocity *= damping;

	if (all(lessThan(abs(velocity), rb->minimumVelocity))) {
		velocity = { 0.0, 0.0 };
	}

	// Remove the forces after integrating
	rb->forces.clear();
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

	auto manifold = Manifold { .normal = normalize(pos1 - pos2), .depth = penetration };

	// tangent direction perpendicular to normal
	dvec2 base_point = pos2 + manifold.normal * (rb2->radius - manifold.depth);
	manifold.contact1 = base_point;
	manifold.contact2 = base_point;
	manifold.contactCount = 1;

	if (rb1->debug.showManifolds) {
		manifold.Debug();
	}
	return manifold;
}

void RbRbResolution(Rigidbody* rb1, Rigidbody* rb2, Manifold manifold) {
	dvec2 velocity1 = rb1->velocity;
	dvec2 velocity2 = rb2->velocity;

	dvec2 position1 = rb1->GetPosition();
	dvec2 position2 = rb2->GetPosition();

	// Early out if both bodies have infinite mass
	if (rb1->mass <= 0.0 && rb2->mass <= 0.0) {
		return;
	}

	double inv_mass1 = (rb1->mass > 0.0) ? 1.0 / rb1->mass : 0.0;
	double inv_mass2 = (rb2->mass > 0.0) ? 1.0 / rb2->mass : 0.0;

	double inv_mass_sum = inv_mass1 + inv_mass2;
	if (inv_mass_sum <= 0.0) {
		return;
	}

	dvec2 normal = manifold.normal;
	dvec2 contact_tangent = { -normal.y, normal.x };

	// Relative velocity
	dvec2 relative_velocity = velocity1 - velocity2;

	// Unfold velocity in normal and tangential components
	double normal_speed = dot(relative_velocity, normal);
	double tangent_speed = dot(relative_velocity, contact_tangent);

	// Only resolve if bodies are moving towards each other
	if (normal_speed < 0.0) {
		// Restitution disabled below threshold to prevent jitter
		double restitution1 = (std::abs(normal_speed) < rb1->restitutionThreshold) ? 0.0 : rb1->restitution;
		double restitution2 = (std::abs(normal_speed) < rb2->restitutionThreshold) ? 0.0 : rb2->restitution;

		double restitution = std::min(restitution1, restitution2);

		// Normal impulse
		// Jn = -(1 + e) * vn / (invMass1 + invMass2)
		double normal_impulse = -(1.0 + restitution) * normal_speed / inv_mass_sum;

		// Friction impulse
		double friction = std::sqrt(rb1->friction * rb2->friction);
		double max_friction_impulse = friction * std::abs(normal_impulse);

		// Tangential impulse needed to cancel tangential speed
		double tangential_impulse = -tangent_speed / inv_mass_sum;
		tangential_impulse = clamp(tangential_impulse, -max_friction_impulse, max_friction_impulse);

		// Apply impulses
		dvec2 impulse = normal_impulse * normal + tangential_impulse * contact_tangent;

		velocity1 += impulse * inv_mass1;
		velocity2 -= impulse * inv_mass2;
	}

	// Positional correction
	double penetration_correction = std::max(manifold.depth - PhysicsSystem::pos_slop(), 0.0) * PhysicsSystem::pos_ptc();
	dvec2 correction = (penetration_correction / inv_mass_sum) * normal;

	position1 += correction * inv_mass1;
	position2 -= correction * inv_mass2;

	// Velocity correction
	rb1->velocity = velocity1;
	rb2->velocity = velocity2;

	rb1->SetPosition(position1);
	rb2->SetPosition(position2);
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
		double overlap = std::min(max_proj - rb_min_proj, rb_max_proj - min_proj);

		// find closest point on the edge segment to the rigidbody
		dvec2 a = edge.p1;
		dvec2 b = edge.p2;
		dvec2 ab = b - a;
		double t = dot(rb_pos - a, ab) / dot(ab, ab);
		t = std::clamp(t, 0.0, 1.0);
		dvec2 closest = a + ab * t;

		// bias depth so nearer parallel edges win
		double dist = length(rb_pos - closest);
		overlap += PhysicsSystem::eps() * (dist * dist);

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

	if (rb->debug.showManifolds) {
		best.Debug();
	}

	return best;
}

void RbMeshResolution(Rigidbody* rb, ConvexCollider* c, Manifold manifold) {
	dvec2 velocity = rb->velocity;
	dvec2 position = rb->GetPosition();

	// Early out if body is effectively infinite mass
	if (rb->mass <= 0.0) {
		return;
	}

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

std::optional<dvec2> RbRayCollision(Line* ray, Rigidbody* rb) {
	std::optional<dvec2> result = std::nullopt;
	dvec2 pt1 = ray->p1 - rb->GetPosition();
	dvec2 pt2 = ray->p2 - rb->GetPosition();
  dvec2 min_distance = cross(dvec3(pt1.x, pt1.y, 0.0f), dvec3(pt2.x, pt2.y, 0.0f)) / length(pt2 - pt1);

	if (length2(min_distance) >= rb->radius * rb->radius)
		return std::nullopt;

	result = rb->GetPosition();
	return result;
}

}
