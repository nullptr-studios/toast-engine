#include "RigidbodyDynamics.hpp"

#include "PhysicsSystem.hpp"

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Profiler.hpp>
#include <Engine/Core/Time.hpp>
#include <Engine/Physics/Rigidbody.hpp>
#include <glm/glm.hpp>

using namespace physics;
using namespace glm;

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
