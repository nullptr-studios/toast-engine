#include "dynamic_rigidbody.hpp"

#include <toast/physics/simulator.hpp>

namespace physics {

void DynamicRigidbody::sleep() {
	Simulator::sleepBody(bodyID());
}

void DynamicRigidbody::wake() {
	Simulator::wakeBody(bodyID());
}

void DynamicRigidbody::setLinearVelocity(const glm::vec3& velocity) {
	Simulator::setBodyLinearVelocity(bodyID(), velocity);
}

void DynamicRigidbody::setPosition(const glm::vec3& position) {
	Simulator::setBodyTransform(bodyID(), position, world_rotation);
}

void DynamicRigidbody::setRotation(const glm::quat& rotation) {
	Simulator::setBodyTransform(bodyID(), world_position, rotation);
}

void DynamicRigidbody::publishPhysicsState(
    bool is_awake, const glm::vec3& current_linear_velocity, const glm::vec3& current_angular_velocity
) {
	const bool state_changed = awake != is_awake;
	awake = is_awake;
	linear_velocity = current_linear_velocity;
	angular_velocity = current_angular_velocity;

	if (not state_changed) {
		return;
	}

	if (awake) {
		woke_up.fire();
	} else {
		went_to_sleep.fire();
	}
}

}
