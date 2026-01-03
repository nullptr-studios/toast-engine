#include "BoxDynamics.hpp"

#include "ConvexCollider.hpp"
#include "PhysicsSystem.hpp"

#include <Engine/Core/Time.hpp>
#include <Engine/Physics/BoxRigidbody.hpp>
#include <Engine/Renderer/DebugDrawLayer.hpp>

namespace physics {
using namespace glm;

void BoxManifold::Debug() const {
	renderer::DebugCircle(contact1, 0.1, { 0.0f, 1.0f, 0.0f, 1.0f });
	renderer::DebugLine(contact1, contact1 + (normal * depth), { 1.0f, 0.0f, 1.0f, 1.0f });
	if (contactCount == 2) {
		renderer::DebugCircle(contact2, 0.1, { 0.0f, 1.0f, 0.0f, 1.0f });
		renderer::DebugLine(contact2, contact2 + (normal * depth), { 1.0f, 0.0f, 1.0f, 1.0f });
	}
}

std::optional<glm::dvec2> LineLineCollision(const Line& a, const Line& b) {
	// directions
	glm::dvec2 a_vec = a.p2 - a.p1;
	glm::dvec2 b_vec = b.p2 - b.p1;

	glm::dvec2 start_delta = b.p1 - a.p1;

	double cross_a_b = determinant(dmat2{ a_vec, b_vec });           // a_vec x b_vec
	double cross_delta_b = determinant(dmat2{ start_delta, b_vec }); // (b_start - a_start) x b_vec
	double cross_delta_a = determinant(dmat2{ start_delta, a_vec }); // (b_start - a_start) x a_vec

	// Parallel (including colinear)
	if (std::abs(cross_a_b) < PhysicsSystem::eps_small()) {
		return std::nullopt;
	}

	double t_on_a = cross_delta_b / cross_a_b; // position along segment a
	double t_on_b = cross_delta_a / cross_a_b; // position along segment b

	// Intersection must lie within both segments
	if (t_on_a < 0.0 || t_on_a > 1.0 || t_on_b < 0.0 || t_on_b > 1.0) return std::nullopt;

	return dvec2 { a.p1 } + t_on_a * a_vec;
}

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

auto BoxBoxCollision(BoxRigidbody* rb1, BoxRigidbody* rb2) -> std::optional<BoxManifold> {
	return std::nullopt;
}

void BoxBoxResolution(BoxRigidbody* rb1, BoxRigidbody* rb2, BoxManifold manifold) { }

static auto ClipLineSegmentToLine(dvec2 p1, dvec2 p2, dvec2 normal, dvec2 offset) -> std::vector<dvec2> {
	std::vector<dvec2> points;
	points.reserve(2);

	double distance1 = dot(p1 - offset, normal);
	double distance2 = dot(p2 - offset, normal);

	// if the points are behind the plane, don't clip
	if (distance1 <= 0.0) points.emplace_back(p1);
	if (distance2 <= 0.0) points.emplace_back(p2);

	// if one is in front of the plane, clip it to the intersection point
	if (std::signbit(distance1) != std::signbit(distance2) && points.size() < 2) {
		const double pct_across = distance2 / (distance2 - distance1);
		const dvec2 intersection_point = (p1 - p2) * pct_across;
		points.emplace_back(intersection_point);
	}

	return points;
}

auto BoxMeshCollision(BoxRigidbody* rb, ConvexCollider* c) -> std::optional<BoxManifold> {
	dvec2 rb_pos = rb->GetPosition();
	std::list<BoxManifold> manifolds;

	int edge_count = 0;
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
		auto manifold = BoxManifold{
			.normal = edge.normal,
			.contact1 = {0.0, 0.0},
			.contact2 = {0.0, 0.0},
			.contactCount = -1,
			.depth = overlap
		};
		// clang-format on

		std::vector<dvec2> points;
		for (const auto& e : rb->GetEdges()) {
			auto p = LineLineCollision(e, edge);
			if (p.has_value()) points.emplace_back(p.value());
		}

		if (points.size() >= 2) {
			manifold.contact1 = points[0];
			manifold.contact2 = points[1];
			manifold.contactCount = 2;
		} else if (points.size() == 1) {
			manifold.contact1 = points[0];
			manifold.contactCount = 1;
		} else {
			manifold.contact1 = rb->GetPosition();
			manifold.contact2 = rb->GetPosition();
			manifold.contactCount = 1;
		}

		manifolds.emplace_back(manifold);
	}

	// select the axis with the least penetration depth
	auto it = std::ranges::min_element(manifolds, {}, &BoxManifold::depth);
	if (it == manifolds.end()) {
		return std::nullopt;
	}
	auto best = *it;

	if (rb->debug.showManifolds) best.Debug();
	return best;
}

void BoxMeshResolution(BoxRigidbody* rb, ConvexCollider* c, BoxManifold manifold) {
	dvec2 position = rb->GetPosition();
	dvec2& velocity = rb->velocity;
	double& angular_velocity = rb->angularVelocity;

	// Skip if body has infinite mass
	if (rb->mass <= 0.0) return;
	double inv_mass = 1.0 / rb->mass;

	// unfold velocity in normal and tangencial
	dvec2 contact_tangent = {-manifold.normal.y, manifold.normal.x};
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
	rb->SetPosition(position);

	// velocity correction
	double residual_normal_speed = dot(velocity, manifold.normal);
	if (residual_normal_speed < 0.0) {
		velocity -= residual_normal_speed * manifold.normal;
	}
}

}
