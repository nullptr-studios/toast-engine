/**
 * @file pin_joint.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Bodies are maintained by one single point
 */

#pragma once
#include "joint3d.hpp"

namespace physics {

class [[ToastNode, Color("Green")]] TOAST_API PinJoint : public physics::Joint3D {
public:
	/// Percentage of the error between the two points
	[[Reflect, Range(0.0, 1.0)]]
	float bias = 0.3f;

	/// Most the joint pushes with in one iteration
	[[Reflect]]
	float impulse_clamp = 0.0f;

	[[Reflect, Unit("m"), Range(0.0, 0.1)]]
	float friction = k_default_joint_friction;

	[[nodiscard]]
	auto makeParams() const -> JointParams override {
		return PinParams {.bias = bias, .impulse_clamp = impulse_clamp, .friction = friction};
	}

protected:
	void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const override;
};

}
