/**
 * @file slider_joint.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Lets two bodies slide along one axis
 */

#pragma once
#include "joint3d.hpp"

namespace physics {

class [[ToastNode, Icon("SliderJoint"), Color("Green")]] TOAST_API SliderJoint : public physics::Joint3D {
public:
	/// Percentage of the error that is closed every step
	[[Reflect, Range(0.0, 1.0)]]
	float bias = 0.3f;

	[[Reflect, Unit("m")]]
	float linear_limit_lower = -1.0f;

	[[Reflect, Unit("m")]]
	float linear_limit_upper = 1.0f;

	[[Reflect, ReadOnly, Unit("m")]]
	float current_distance = 0.0f;

	[[nodiscard]]
	auto makeParams() const -> JointParams override {
		return SliderParams {.bias = bias, .linear_limit_lower = linear_limit_lower, .linear_limit_upper = linear_limit_upper};
	}

protected:
	void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const override;
	void applyState(const JointState& state) override { current_distance = state.position; }
};

}
