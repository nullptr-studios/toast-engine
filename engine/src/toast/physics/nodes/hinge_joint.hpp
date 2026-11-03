/**
 * @file hinge_joint.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Like a door hinge
 */

#pragma once
#include "joint3d.hpp"

namespace physics {

class [[ToastNode, Icon("HingeJoint"), Color("Green")]] TOAST_API HingeJoint : public physics::Joint3D {
public:
	/// Share of the error that is closed every step
	[[Reflect, Range(0.0, 1.0)]]
	float bias = 0.3f;

	/// Torque that holds the hinge against turning per newton it carries, the friction of the material times the radius of
	/// the pin. Zero swings forever
	[[Reflect, Unit("m"), Range(0.0, 0.1)]]
	float friction = k_default_joint_friction;

	[[Reflect, Group("Angular Limit")]]
	bool limit_enabled = false;

	/// Angles go around the axis from the x of the joint to the body that turns
	[[Reflect, Group("Angular Limit"), Unit("°")]]
	float limit_lower = -90.0f;

	[[Reflect, Group("Angular Limit"), Unit("°")]]
	float limit_upper = 90.0f;

	[[Reflect, Group("Angular Limit"), Range(0.0, 1.0)]]
	float limit_bias = 0.3f;

	[[Reflect, Group("Angular Limit"), Range(0.0, 1.0)]]
	float limit_restitution = 0.0f;

	[[Reflect, Group("Motor")]]
	bool motor_enabled = false;

	[[Reflect, Group("Motor"), Unit("°/s")]]
	float motor_target_velocity = 0.0f;

	/// Most the motor pushes with in one step
	[[Reflect, Group("Motor")]]
	float motor_max_impulse = 1.0f;

	[[Reflect, Group("State"), ReadOnly, Unit("°")]]
	float current_angle = 0.0f;

	/// Turns the motor on at the speed with the push it is allowed
	[[Reflect]]
	void setMotor(float target_velocity_degrees, float max_impulse);

	[[nodiscard]]
	auto makeParams() const -> JointParams override;

	[[nodiscard]]
	auto limitHandles() const -> LimitHandles override;

	[[nodiscard]]
	auto limitsAreAngles() const -> bool override {
		return true;
	}

	[[nodiscard]]
	auto limitValue(toast::GizmoHandle handle) const -> float override;
	void setLimitValue(toast::GizmoHandle handle, float value) override;

	[[nodiscard]]
	auto limitField(toast::GizmoHandle handle) const -> const char* override;

	static constexpr float k_arc_radius = 0.35f;

protected:
	void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const override;

	void applyState(const JointState& state) override;
	void updateInspectorMessages() override;
};

}
