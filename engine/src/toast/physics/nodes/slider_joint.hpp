/**
 * @file slider_joint.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Lets two bodies slide along one axis
 */

#pragma once
#include "joint3d.hpp"

#include <algorithm>

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

	[[Reflect, Range(0.0, 1.0)]]
	float limit_restitution = 0.0f;

	[[Reflect, ReadOnly, Unit("m")]]
	float current_distance = 0.0f;

	[[nodiscard]]
	auto makeParams() const -> JointParams override {
		return SliderParams {
		  .bias = bias,
		  .linear_limit_lower = linear_limit_lower,
		  .linear_limit_upper = linear_limit_upper,
		  .limit_restitution = limit_restitution
		};
	}

	[[nodiscard]]
	auto limitHandles() const -> LimitHandles override {
		return {
		  {LimitHandle {.handle = toast::GizmoHandle::size_neg_x, .local = {linear_limit_lower, 0.0f, 0.0f}},
			 LimitHandle {.handle = toast::GizmoHandle::size_pos_x, .local = {linear_limit_upper, 0.0f, 0.0f}}},
		  2,
		};
	}

	[[nodiscard]]
	auto limitValue(toast::GizmoHandle handle) const -> float override {
		return handle == toast::GizmoHandle::size_neg_x ? linear_limit_lower : linear_limit_upper;
	}

	void setLimitValue(toast::GizmoHandle handle, float value) override {
		if (handle == toast::GizmoHandle::size_neg_x) {
			linear_limit_lower = std::min(value, linear_limit_upper);
		} else if (handle == toast::GizmoHandle::size_pos_x) {
			linear_limit_upper = std::max(value, linear_limit_lower);
		}
	}

	[[nodiscard]]
	auto limitField(toast::GizmoHandle handle) const -> const char* override {
		if (handle == toast::GizmoHandle::size_neg_x) {
			return "linear_limit_lower";
		}
		return handle == toast::GizmoHandle::size_pos_x ? "linear_limit_upper" : nullptr;
	}

protected:
	void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const override;

	void applyState(const JointState& state) override { current_distance = state.position; }
};

}
