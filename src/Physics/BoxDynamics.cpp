#include "BoxDynamics.hpp"

#include "ConvexCollider.hpp"
#include "PhysicsSystem.hpp"

#include <Toast/Physics/BoxRigidbody.hpp>
#include <Toast/Renderer/DebugDrawLayer.hpp>
#include <Toast/Time.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include "Toast/Resources/Mesh.hpp"
#include "glm/gtx/vector_angle.hpp"
#include "glm/gtx/vector_query.hpp"

#include <glm/gtx/norm.hpp>

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

	double cross_a_b = determinant(dmat2 { a_vec, b_vec });              // a_vec x b_vec
	double cross_delta_b = determinant(dmat2 { start_delta, b_vec });    // (b_start - a_start) x b_vec
	double cross_delta_a = determinant(dmat2 { start_delta, a_vec });    // (b_start - a_start) x a_vec

	// Parallel (including colinear)
	if (std::abs(cross_a_b) < PhysicsSystem::eps_small()) {
		return std::nullopt;
	}

	double t_on_a = cross_delta_b / cross_a_b;    // position along segment a
	double t_on_b = cross_delta_a / cross_a_b;    // position along segment b

	// Intersection must lie within both segments
	if (t_on_a < 0.0 || t_on_a > 1.0 || t_on_b < 0.0 || t_on_b > 1.0) {
		return std::nullopt;
	}

	return dvec2 { a.p1 } + t_on_a * a_vec;
}

void BoxKinematics(BoxRigidbody* rb) {
	auto& velocity = rb->velocity;
	auto& angular_velocity = rb->angularVelocity;

	// Avoid boxes with 0 or less mass
	if (rb->mass < 0.1) {
		rb->mass = 1.0;
	}

	// Sum torques
	double torques = std::ranges::fold_left(rb->torques, 0.0, std::plus {});
	// Compute inertia
	dvec2 half_size = rb->size / 2.0;
	double inertia = (rb->mass * (half_size.x * half_size.x + half_size.y * half_size.y)) / 12.0;
	inertia = (inertia < 0.001) ? 1.0 : inertia;
	// Calculate angular acceleration
	double angular_accel = torques / inertia;

	// Sum forces
	glm::dvec2 forces = std::ranges::fold_left(rb->forces, glm::dvec2 { 0.0 }, std::plus {});
	glm::dvec2 accel = (forces / rb->mass) + (PhysicsSystem::gravity() * dvec2 { rb->gravityScale });

	// Integrate velocities
	velocity += accel * Time::fixed_delta();
	angular_velocity += angular_accel * Time::fixed_delta();

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
	PROFILE_ZONE_C(0x00FF00);
	dvec2 rb1_pos = rb1->GetPosition();
	std::list<BoxManifold> manifolds;

	const auto& aabb1 = rb1->GetAABB();
	const auto& aabb2 = rb2->GetAABB();

	if (aabb1.max.x < aabb2.min.x || aabb1.min.x > aabb2.max.x || aabb1.max.y < aabb2.min.y || aabb1.min.y > aabb2.max.y) {
		return std::nullopt;
	}

	const auto& rb1_points = rb1->GetPoints();
	const auto& rb2_points = rb2->GetPoints();
	const auto& rb2_edges = rb2->GetEdges();

	int edge_count = 0;
	// This method will use the SAT algorithm
	for (const auto& edge : rb2_edges) {
		// get the maximum and minimum points of the collider projected onto the edge normal
		double min_collider = dot(dvec2 { rb2_points[0] }, edge.normal);
		double max_collider = min_collider;

		for (int i = 1; i < rb2_points.size(); i++) {
			double proj = dot(dvec2 { rb2_points[i] }, edge.normal);
			min_collider = std::min(proj, min_collider);
			max_collider = std::max(proj, max_collider);
		}

		// project rigidbody vertices onto the same axis
		double min_rb = dot(dvec2 { rb1_points[0] }, edge.normal);
		double max_rb = min_rb;

		for (int i = 1; i < rb1_points.size(); i++) {
			double proj = dot(dvec2 { rb1_points[i] }, edge.normal);
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
		for (dvec2 p : rb1_points) {
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
		for (const auto& e : rb1->GetEdges()) {
			auto p = LineLineCollision(e, edge);
			if (p.has_value()) {
				points.emplace_back(p.value());
			}
		}

		if (points.size() >= 2) {
			manifold.contact1 = points[0];
			manifold.contact2 = points[1];
			manifold.contactCount = 2;
		} else if (points.size() == 1) {
			manifold.contact1 = points[0];
			manifold.contactCount = 1;
		} else {
			manifold.contact1 = rb1->GetPosition();
			manifold.contact2 = rb1->GetPosition();
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

	if (rb1->debug.showManifolds) {
		best.Debug();
	}
	return best;
}

void BoxBoxResolution(BoxRigidbody* rb1, BoxRigidbody* rb2, BoxManifold manifold) {
	PROFILE_ZONE_C(0xFF00FF);
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
	dvec2 relative_velocity = velocity2 - velocity1;

	// Unfold velocity in normal and tangential components
	double normal_speed = dot(relative_velocity, normal);
	double tangent_speed = dot(relative_velocity, contact_tangent);

	double width1 = rb1->size.x / 2;
	double height1 = rb1->size.y / 2;
	double inertia1 = (rb1->mass * (width1 * width1 + height1 * height1)) / 12.0;
	double inv_inertia1 = inertia1 > 0.0 ? 1.0 / inertia1 : 0.0;

	double width2 = rb2->size.x / 2;
	double height2 = rb2->size.y / 2;
	double inertia2 = (rb2->mass * (width2 * width2 + height2 * height2)) / 12.0;
	double inv_inertia2 = inertia2 > 0.0 ? 1.0 / inertia2 : 0.0;

	dvec2 contact = (manifold.contactCount == 2) ? (manifold.contact1 + manifold.contact2) * 0.5 : manifold.contact1;
	dvec2 r1 = contact - position1;
	dvec2 r2 = contact - position2;
	if (length2(r1) > PhysicsSystem::eps_small() && !isNormalized(r1, 1e-2)) {
		r1 = normalize(r1);
	}
	if (length2(r2) > PhysicsSystem::eps_small() && !isNormalized(r2, 1e-2)) {
		r2 = normalize(r2);
	}

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

		velocity1 -= impulse * inv_mass1;
		velocity2 += impulse * inv_mass2;

		if (!isNormalized(impulse, 1e-2)) {
			impulse = normalize(impulse);
		}
		if (abs(dot(r1, -impulse)) < 1) {
			double angle_to_rotate1 = orientedAngle(r1, -impulse) - pi<double>();
			angle_to_rotate1 *= cos(angle_to_rotate1);
			angle_to_rotate1 *= sin(angle_to_rotate1);
			rb1->angularVelocity += angle_to_rotate1;
		}

		if (abs(dot(r2, impulse)) < 1) {
			double angle_to_rotate2 = orientedAngle(r2, impulse) - pi<double>();
			angle_to_rotate2 *= cos(angle_to_rotate2);
			angle_to_rotate2 *= sin(angle_to_rotate2);
			rb2->angularVelocity += angle_to_rotate2;
		}
	}

	// Positional correction
	double penetration_correction = std::max(manifold.depth - PhysicsSystem::pos_slop(), 0.0) * PhysicsSystem::pos_ptc();
	dvec2 correction = (penetration_correction / inv_mass_sum) * normal;

	position1 -= correction * inv_mass1;
	position2 += correction * inv_mass2;

	rb1->SetPosition(position1);
	rb2->SetPosition(position2);

	// Velocity correction
	auto velocity_correction = [&](BoxRigidbody* rb, dvec2 v) -> dvec2 {
		double normal_velocity = dot(v, normal);
		if (std::abs(normal_velocity) < rb->minimumVelocity.y) {
			// Kill tiny normal velocity
			v -= normal_velocity * normal;
		}

		if (all(lessThan(abs(v), dvec2 { rb->minimumVelocity }))) {
			v = { 0.0, 0.0 };
		}

		return v;
	};

	rb1->velocity = velocity_correction(rb1, velocity1);
	rb2->velocity = velocity_correction(rb2, velocity2);
}

static auto ClipLineSegmentToLine(dvec2 p1, dvec2 p2, dvec2 normal, dvec2 offset) -> std::vector<dvec2> {
	std::vector<dvec2> points;
	points.reserve(2);

	double distance1 = dot(p1 - offset, normal);
	double distance2 = dot(p2 - offset, normal);

	// if the points are behind the plane, don't clip
	if (distance1 <= 0.0) {
		points.emplace_back(p1);
	}
	if (distance2 <= 0.0) {
		points.emplace_back(p2);
	}

	// if one is in front of the plane, clip it to the intersection point
	if (std::signbit(distance1) != std::signbit(distance2) && points.size() < 2) {
		const double pct_across = distance2 / (distance2 - distance1);
		const dvec2 intersection_point = (p1 - p2) * pct_across;
		points.emplace_back(intersection_point);
	}

	return points;
}

auto BoxMeshCollision(BoxRigidbody* rb, ConvexCollider* c) -> std::optional<BoxManifold> {
	PROFILE_ZONE_C(0xFF0000);
	dvec2 rb_pos = rb->GetPosition();
	std::list<BoxManifold> manifolds;

	const auto& colliderAABB = c->getAABB();
	const auto& rbAABB = rb->GetAABB();

	if (rbAABB.max.x < colliderAABB.min.x || rbAABB.min.x > colliderAABB.max.x || rbAABB.max.y < colliderAABB.min.y ||
	    rbAABB.min.y > colliderAABB.max.y) {
		return std::nullopt;
	}

	const auto& rb_points = rb->GetPoints();

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

		// project rigidbody vertices onto the same axis
		double min_rb = dot(dvec2 { rb_points[0] }, edge.normal);
		double max_rb = min_rb;
		bool not_needed = false;
		for (int i = 1; i < rb_points.size(); i++) {
			double proj = dot(dvec2 { rb_points[i] }, edge.normal);
			min_rb = std::min(proj, min_rb);
			max_rb = std::max(proj, max_rb);

			// we DO NOT want to check with rigidbodies that are behind the normal
			if (proj < -rb->size.y) {
				not_needed = true;
				break;
			}
		}

		if (not_needed == true) {
			continue;
		}

		// if theres no overlap on this axis, there is no collision at all
		if (max_rb < min_collider || min_rb > max_collider) {
			return std::nullopt;
		}

		// find where along the line is the object
		double distance_along_line = dot(rb_pos - edge.p1, edge.tangent);
		glm::dvec2 normal;
		if (distance_along_line < 0.0f) {
			// Case 1: rigidbody is before segment
			normal = rb_pos - edge.p1;
		} else if (distance_along_line > edge.length) {
			// Case 2: rigidbody is after segment
			normal = rb_pos - edge.p2;
		} else {
			// Case 3: rigidbody is inside the line
			normal = edge.normal;
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
			if (p.has_value()) {
				points.emplace_back(p.value());
			}
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

	if (rb->debug.showManifolds) {
		best.Debug();
	}
	return best;
}

void BoxMeshResolution(BoxRigidbody* rb, ConvexCollider* c, BoxManifold manifold) {
	PROFILE_ZONE_C(0x00FFFF);
	dvec2 position = rb->GetPosition();
	dvec2& velocity = rb->velocity;
	double& angular_velocity = rb->angularVelocity;

	// Skip if body has infinite mass
	if (rb->mass <= 0.0) {
		return;
	}
	double inv_mass = 1.0 / rb->mass;

	// moment of inertia for box
	double width = rb->size.x / 2;
	double height = rb->size.y / 2;
	double inertia = (rb->mass * (width * width + height * height)) / 12.0;
	double inv_inertia = inertia > 0.0 ? 1.0 / inertia : 0.0;

	dvec2 contact = (manifold.contactCount == 2) ? (manifold.contact1 + manifold.contact2) * 0.5 : manifold.contact1;
	dvec2 r = contact - position;
	if (length2(r) > PhysicsSystem::eps_small() && !isNormalized(r, 1e-2)) {
		r = normalize(r);
	}

	// unfold velocity in normal and tangencial
	dvec2 contact_tangent = { -manifold.normal.y, manifold.normal.x };
	double normal_speed = dot(velocity, manifold.normal);
	double tangent_speed = dot(velocity, contact_tangent);

	// only apply restitution if we're moving faster than the restitution threshold
	double restitution = (std::abs(normal_speed) < rb->restitutionThreshold) ? 0.0 : rb->restitution;

	// only resolve if we're going towards the object
	if (normal_speed < 0.0) {
		double normal_lever_arm = determinant(dmat2 { r, manifold.normal });
		double tangent_lever_arm = determinant(dmat2 { r, contact_tangent });
		double normal_effective_mass = inv_mass + (normal_lever_arm * normal_lever_arm * inv_inertia);
		double tangent_effective_mass = inv_mass + (tangent_lever_arm * tangent_lever_arm * inv_inertia);

		// Normal impulse (bounce response)
		double normal_impulse = -(1.0 + restitution) * normal_speed / normal_effective_mass;

		// Coulomb friction
		double max_friction_impulse = c->friction * std::abs(normal_impulse);

		// calculate tangential impulse to cancel tangential speed
		double tangential_impulse = -tangent_speed / tangent_effective_mass;
		tangential_impulse = clamp(tangential_impulse, -max_friction_impulse, max_friction_impulse);

		// total impulse
		dvec2 impulse = normal_impulse * manifold.normal + tangential_impulse * contact_tangent;

		// Apply impulses to velocity
		velocity += impulse * inv_mass;

		if (!isNormalized(impulse, 1e-2)) {
			impulse = normalize(impulse);
		}
		if (abs(dot(r, impulse)) < 1) {
			double angle_to_rotate = orientedAngle(r, impulse) - pi<double>();
			angle_to_rotate *= cos(angle_to_rotate);
			angle_to_rotate *= sin(angle_to_rotate);
			rb->angularVelocity += angle_to_rotate;
		}
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
