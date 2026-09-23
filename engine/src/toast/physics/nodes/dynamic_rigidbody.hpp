/**
 * @file DynamicRigidbody.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "rigidbody.hpp"

namespace physics {

class [[ToastNode, Icon("RigidBody")]] TOAST_API DynamicRigidbody : public physics::Rigidbody {
	friend class Simulator;

public:
	DynamicRigidbody() : Rigidbody(BodyType::dynamic_body) { }

	signals::Signal<> went_to_sleep;
	signals::Signal<> woke_up;

	[[Reflect]]
	void sleep();
	[[Reflect]]
	void wake();
	[[Reflect]]
	void setLinearVelocity(const glm::vec3& velocity);
	[[Reflect]]
	void setPosition(const glm::vec3& position);
	[[Reflect]]
	void setRotation(const glm::quat& rotation);

	[[Reflect, Unit("kg")]]
	float mass = 1.0f;
	[[Reflect]]
	float gravity_scale = 1.0f;
	[[Reflect]]
	bool allow_sleep = true;
	[[Reflect, ReadOnly]]
	bool awake = true;

	// TODO:

	[[Reflect, Group("Mass Distribution"), Unit("m"), ReadOnly]]
	glm::vec3 center_of_mass = {};
	[[Reflect, Group("Mass Distribution"), Unit("kg•m²"), ReadOnly]]
	glm::vec3 inertia = {};

	[[Reflect, Group("Velocities"), Unit("m/s"), ReadOnly]]
	glm::vec3 linear_velocity = {};
	[[Reflect, Group("Velocities"), Unit("rad/s"), ReadOnly]]
	glm::vec3 angular_velocity = {};

	[[Reflect, Group("Constant Forces"), Unit("N"), ReadOnly]]
	glm::vec3 constant_force = {};
	[[Reflect, Group("Constant Forces"), Unit("N•m"), ReadOnly]]
	glm::vec3 constant_torque = {};

private:
	void publishPhysicsState(bool is_awake, const glm::vec3& current_linear_velocity, const glm::vec3& current_angular_velocity);

	void configureBodyDescriptor(BodyDescriptor& descriptor) const override {
		descriptor.mass = mass;
		descriptor.gravity_scale = gravity_scale;
		descriptor.allow_sleep = allow_sleep;
	}
};

}
