#include "dynamic_rigidbody.hpp"

#include <toast/physics/simulator.hpp>

namespace physics {

void DynamicRigidbody::sleep() {
	if (not Simulator::onSimulatorThread()) {
		Simulator::request([self = box().as<DynamicRigidbody>()]() mutable {
			if (self.exists()) {
				self->sleep();
			}
		});
		return;
	}
	Simulator::runPendingRequests();
	Simulator::requestSleep(bodyID());
}

void DynamicRigidbody::wake() {
	if (not Simulator::onSimulatorThread()) {
		Simulator::request([self = box().as<DynamicRigidbody>()]() mutable {
			if (self.exists()) {
				self->wake();
			}
		});
		return;
	}
	Simulator::runPendingRequests();
	Simulator::requestWake(bodyID());
}

void DynamicRigidbody::setLinearVelocity(const glm::vec3& velocity) {
	if (not Simulator::onSimulatorThread()) {
		Simulator::request([self = box().as<DynamicRigidbody>(), velocity]() mutable {
			if (self.exists()) {
				self->setLinearVelocity(velocity);
			}
		});
		return;
	}
	Simulator::runPendingRequests();
	Simulator::setBodyLinearVelocity(bodyID(), velocity);
}

void DynamicRigidbody::setPosition(const glm::vec3& position) {
	if (not Simulator::onSimulatorThread()) {
		Simulator::request([self = box().as<DynamicRigidbody>(), position]() mutable {
			if (self.exists()) {
				self->setPosition(position);
			}
		});
		return;
	}
	Simulator::runPendingRequests();
	Simulator::setBodyTransform(bodyID(), position, world_rotation);
}

void DynamicRigidbody::setRotation(const glm::quat& rotation) {
	if (not Simulator::onSimulatorThread()) {
		Simulator::request([self = box().as<DynamicRigidbody>(), rotation]() mutable {
			if (self.exists()) {
				self->setRotation(rotation);
			}
		});
		return;
	}
	Simulator::runPendingRequests();
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
