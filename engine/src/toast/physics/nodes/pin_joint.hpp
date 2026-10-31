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

	/// How much of the speed between the two points is cancelled
	[[Reflect]]
	float damping = 1.0f;

	/// Most the joint pushes with in one iteration
	[[Reflect]]
	float impulse_clamp = 0.0f;

	[[nodiscard]]
	auto makeParams() const -> JointParams override {
		return PinParams {.bias = bias, .damping = damping, .impulse_clamp = impulse_clamp};
	}

protected:
	void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const override;
};

}
