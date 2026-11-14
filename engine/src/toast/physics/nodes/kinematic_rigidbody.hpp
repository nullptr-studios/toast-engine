/**
 * @file KinematicRigidbody.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "rigidbody.hpp"

namespace physics {

class [[ToastNode, Icon("CharacterBody")]] TOAST_API KinematicRigidbody : public physics::Rigidbody {
	friend class Simulator;

public:
	KinematicRigidbody() : Rigidbody(BodyType::kinematic_body) { }

	[[Reflect, Group("Velocities"), Unit("m/s"), ReadOnly]]
	glm::vec3 linear_velocity = {};
	[[Reflect, Group("Velocities"), Unit("rad/s"), ReadOnly]]
	glm::vec3 angular_velocity = {};

private:
	void configureBodyDescriptor(BodyDescriptor& descriptor) const override {
		descriptor.linear_velocity = linear_velocity;
		descriptor.angular_velocity = angular_velocity;
	}
};

}
