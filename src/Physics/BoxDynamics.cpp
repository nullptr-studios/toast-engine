#include "BoxDynamics.hpp"

#include "ConvexCollider.hpp"
#include "PhysicsSystem.hpp"

#include <Engine/Core/Time.hpp>
#include <Engine/Physics/BoxRigidbody.hpp>

namespace physics {
using namespace glm;

void BoxKinematics(BoxRigidbody* rb) {
	auto& velocity = rb->velocity;
	auto& angular_velocity = rb->angularVelocity;

	// Avoid boxes with 0 or less mass
	if (rb->mass < 0.1) {
		rb->mass = 1.0;
	}

	// Sum forces
	glm::dvec2 forces = std::ranges::fold_left(rb->forces, glm::dvec2 { 0.0 }, std::plus {});
	// TODO: Handle rotation at some point

	glm::dvec2 accel = (forces / rb->mass) + (PhysicsSystem::gravity() * dvec2 { rb->gravityScale });

	// Integrate velocities
	velocity += accel * Time::fixed_delta();
	// angular_velocity += angular_accle * Time::fixed_delta();

	// Apply drag
	const double damping = std::exp(-rb->linearDrag * Time::fixed_delta());
	velocity *= damping;

	const double angular_damping = std::exp(-rb->angularDrag * Time::fixed_delta());
	angular_velocity *= angular_damping;

	// Stop the object if less than minimum velocity
	if (all(lessThan(abs(velocity), rb->minimumVelocity))) {
		velocity = { 0.0, 0.0 };
	}
	if (angular_velocity < rb->minimumAngularVelocity) {
		angular_velocity = 0.0;
	}
}

void BoxIntegration(BoxRigidbody* rb) {
	// Integrate position
	dvec2 pos = rb->GetPosition();
	pos += rb->velocity * Time::fixed_delta();
	rb->SetPosition(pos);

	// Integrate rotation
	double rot = rb->GetRotation();
	rot += rb->angularVelocity * Time::fixed_delta();
	rb->SetRotation(rot);
}

void BoxResetVelocity(BoxRigidbody* rb) {
	// Set the velocity to 0 at the start of the simulation
	rb->velocity = { 0.0, 0.0 };
	rb->angularVelocity = 0.0;
}

auto BoxBoxCollision(BoxRigidbody* rb1, BoxRigidbody* rb2) -> std::optional<Manifold> {
	return std::nullopt;
}

void BoxBoxResolution(BoxRigidbody* rb1, BoxRigidbody* rb2, Manifold manifold) { }

auto BoxMeshCollision(BoxRigidbody* rb, ConvexCollider* c) -> std::optional<Manifold> {
	dvec2 rb_pos = rb->GetPosition();
	std::list<Manifold> manifolds;

	// This method will use the SAT algorithm
	for (const auto& edge : c->edges) {
		// get the maximum and minimum points of the collider projected onto the edge normal
		double min_collider = dot(dvec2 { c->vertices[0] }, edge.normal);
		double max_collider = min_collider;

		for (int i = 1; i < c->vertices.size(); i++) {
			double proj = dot(dvec2 { c->vertices[i] }, edge.normal);
			min_collider = std::min(proj, min_collider);
			max_collider = std::max(proj, max_collider);
		}

		// get the rigidbody OBB vertices
		std::vector<vec2> rb_points = rb->GetPoints();

		// project rigidbody vertices onto the same axis
		double min_rb = dot(dvec2 { rb_points[0] }, edge.normal);
		double max_rb = min_rb;

		for (int i = 1; i < rb_points.size(); i++) {
			double proj = dot(dvec2 { rb_points[i] }, edge.normal);
			min_rb = std::min(proj, min_rb);
			max_rb = std::max(proj, max_rb);
		}

		// if theres no overlap on this axis, there is no collision at all
		if (max_rb < min_collider || min_rb > max_collider) {
			return std::nullopt;
		}

		// compute overlap along the axis (penetration depth candidate)
		double overlap = std::min(max_collider - min_rb, max_rb - min_collider);

		// find closest rigidbody vertex to the edge segment (used for biasing)
		dvec2 a = edge.p1;
		dvec2 b = edge.p2;
		dvec2 ab = b - a;

		double best_dist2 = std::numeric_limits<double>::max();
		for (dvec2 p : rb_points) {
			double t = dot(p - a, ab) / dot(ab, ab);
			t = std::clamp(t, 0.0, 1.0);
			dvec2 closest = a + ab * t;
			double dist2 = dot(p - closest, p - closest);
			best_dist2 = std::min(best_dist2, dist2);
		}

		// bias depth so nearer parallel edges win
		overlap += PhysicsSystem::eps() * best_dist2;

		// store candidate manifold
		// clang-format off
		manifolds.emplace_back(Manifold{
			.normal = edge.normal,
			.contact1 = {0.0, 0.0},
			.contact2 = {0.0, 0.0},
			.contactCount = -1,
			.depth = overlap
		});
		// clang-format on
	}

	// We also need to do SAT for the rigidbody normals
	std::vector<vec2> rb_normals = rb->GetEdges();
	for (dvec2 normal : rb_normals) {
		// project collider vertices onto the rigidbody normal
		double min_collider = dot(dvec2 { c->vertices[0] }, normal);
		double max_collider = min_collider;

		for (int i = 1; i < c->vertices.size(); i++) {
			double proj = dot(dvec2 { c->vertices[i] }, normal);
			min_collider = std::min(proj, min_collider);
			max_collider = std::max(proj, max_collider);
		}

		// project rigidbody vertices onto the same normal
		std::vector<vec2> rb_points = rb->GetPoints();
		double min_rb = dot(dvec2 { rb_points[0] }, normal);
		double max_rb = min_rb;

		for (int i = 1; i < rb_points.size(); i++) {
			double proj = dot(dvec2 { rb_points[i] }, normal);
			min_rb = std::min(proj, min_rb);
			max_rb = std::max(proj, max_rb);
		}

		// separating axis test
		if (max_rb < min_collider || min_rb > max_collider) {
			return std::nullopt;
		}

		// penetration depth along this axis
		double overlap = std::min(max_collider - min_rb, max_rb - min_collider);

		// store candidate manifold
		// clang-format off
		manifolds.emplace_back(Manifold{
			.normal = normal,
			.contact1 = {0.0, 0.0},
			.contact2 = {0.0, 0.0},
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
	// collect rigidbody vertices that are deepest along the collision normal
	std::vector<dvec2> contacts;
	double max_proj = -std::numeric_limits<double>::max();

	for (dvec2 p : rb->GetPoints()) {
		double proj = dot(p, best.normal);
		if (proj > max_proj + PhysicsSystem::eps()) {
			contacts.clear();
			contacts.push_back(p);
			max_proj = proj;
		} else if (std::abs(proj - max_proj) <= PhysicsSystem::eps()) {
			contacts.push_back(p);
		}
	}

	// fill contact data
	if (contacts.size() == 1) {
		best.contact1 = contacts[0];
		best.contact2 = contacts[0];
		best.contactCount = 1;
	} else {
		best.contact1 = contacts[0];
		best.contact2 = contacts[1];
		best.contactCount = 2;
	}

	if (rb->debug.showManifolds) {
		best.Debug();
	}
	return best;
}

void BoxMeshResolution(BoxRigidbody* rb, ConvexCollider* c, Manifold manifold) { }

}
