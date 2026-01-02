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
		manifolds.emplace_back(BoxManifold{
			.normal = edge.normal,
			.contact1 = {0.0, 0.0},
			.contact2 = {0.0, 0.0},
			.contactCount = -1,
			.depth = overlap,
			.colliderEdgeIndex = edge_count,
			.boxEdgeIndex = -1
		});
		// clang-format on
		
		edge_count++;
	}

	// select the axis with the least penetration depth
	auto it = std::ranges::min_element(manifolds, {}, &BoxManifold::depth);
	if (it == manifolds.end()) {
		return std::nullopt;
	}
	auto best = *it;

	// compute contact information for the chosen manifold
	// edge–edge contact: clip incident edge against reference edge

	// find reference edge on collider (normal match)
	const Line* ref = nullptr;
	for (const auto& e : c->edges) {
		if (dot(e.normal, best.normal) > 1.0 - PhysicsSystem::eps()) {
			ref = &e;
			break;
		}
	}
	if (!ref) {
		return best;
	}

	// TODO: get the vertices of the rigidbody that are inside the collider
	// TODO: get their normal
	// TODO: set the contact point as vertices + (vertices_normal * distance_from_vertex_to_edge_on_normal_direction)

	if (best.colliderEdgeIndex != -1) {
		renderer::DebugCircle(c->edges[best.colliderEdgeIndex].p1, 0.1f, {1.0f, 0.0f, 0.0f, 1.0f});
		renderer::DebugCircle(c->edges[best.colliderEdgeIndex].p2, 0.1f, {1.0f, 0.0f, 0.0f, 1.0f});
	}

	std::list<glm::dvec2> points;
	glm::dvec2 a = c->edges[best.colliderEdgeIndex].p1;
	glm::dvec2 b = c->edges[best.colliderEdgeIndex].p2;

	auto rb_edges = rb->GetEdges();
	for (const auto& rb_e : rb_edges) {
		glm::dvec2 c = rb_e.p1;
		glm::dvec2 d = rb_e.p2;

		double oa = glm::determinant(glm::mat2((d - a), (c - a)));
		double ob = glm::determinant(glm::mat2((d - c), (b - c)));
		double oc = glm::determinant(glm::mat2((b - a), (c - a)));
		double od = glm::determinant(glm::mat2((b - a), (d - a)));

		// Proper intersection exists iff opposite signs
		if (oa*ob < 0 && oc*od < 0) {
			points.emplace_back((a*ob - b*oa) / (ob-oa));
		}
	}

	for (const auto& p : points) {
		renderer::DebugCircle(p, 0.1f, {0.0f, 1.0f, 0.0f, 1.0f});
	}

	return best;
}

void BoxMeshResolution(BoxRigidbody* rb, ConvexCollider* c, BoxManifold manifold) { }

}
