/**
 * @file fixed_joint.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Welds two bodies
 */

#pragma once
#include "joint3d.hpp"

namespace physics {

class [[ToastNode, Icon("Anchor"), Color("Green")]] TOAST_API FixedJoint : public physics::Joint3D {
public:
	[[Reflect, Range(0.0, 1.0)]]
	float bias = 0.3f;

	[[nodiscard]]
	auto makeParams() const -> JointParams override {
		return FixedParams {.bias = bias};
	}

protected:
	void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const override;
};

}
