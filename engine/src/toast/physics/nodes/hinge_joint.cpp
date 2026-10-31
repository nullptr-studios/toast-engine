#include "hinge_joint.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <toast/renderer/debug/debug_draw.hpp>

namespace physics {

void HingeJoint::setMotor(float target_velocity_degrees, float max_impulse) {
	motor_target_velocity = target_velocity_degrees;
	motor_max_impulse = max_impulse;
	motor_enabled = true;
}

auto HingeJoint::makeParams() const -> JointParams {
	return HingeParams {
	  .bias = bias,
	  .limit_enabled = limit_enabled,
	  .limit_lower = glm::radians(limit_lower),
	  .limit_upper = glm::radians(limit_upper),
	  .limit_bias = limit_bias,
	  .motor_enabled = motor_enabled,
	  .motor_target_velocity = glm::radians(motor_target_velocity),
	  .motor_max_impulse = motor_max_impulse,
	};
}

void HingeJoint::applyState(const JointState& state) {
	current_angle = glm::degrees(state.angle);
}

void HingeJoint::updateInspectorMessages() {
	Joint3D::updateInspectorMessages();

	static const toast::NodeMessage limits_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 14,
	  .text = "The lower limit is above the upper limit",
	};
	static const toast::NodeMessage axis_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 15,
	  .text = "Another hinge between the same two nodes turns around a different axis, the two would fight",
	};

	if (limit_enabled && limit_lower > limit_upper) {
		addInspectorMessage(limits_message);
	} else {
		removeInspectorMessage(limits_message);
	}

	// Two hinges hold a door only when their axes are the same line
	bool bent = false;
	if (parent().exists()) {
		syncWorldTransform();
		const glm::vec3 axis = world_rotation * glm::vec3(0.0f, 0.0f, 1.0f);
		for (const auto& sibling : parent()->children()) {
			const auto other = sibling.as<HingeJoint>();
			if (other.exists() && not(other == box().as<HingeJoint>()) && other->node_a == node_a && other->node_b == node_b) {
				other->syncWorldTransform();
				const glm::vec3 other_axis = other->world_rotation * glm::vec3(0.0f, 0.0f, 1.0f);
				const glm::vec3 apart = other->world_position - world_position;
				const bool parallel = std::abs(glm::dot(axis, other_axis)) > 0.999f;
				const bool same_line = glm::length(glm::cross(apart, axis)) < 0.01f * std::max(1.0f, glm::length(apart));
				bent = bent || not parallel || not same_line;
			}
		}
	}
	if (bent) {
		addInspectorMessage(axis_message);
	} else {
		removeInspectorMessage(axis_message);
	}
}

void HingeJoint::drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const {
	const glm::vec3 axis = axes[2];
	debug::drawLine(origin - (axis * 0.4f), origin + (axis * 0.4f), color);
	if (not limit_enabled) {
		return;
	}

	constexpr float k_radius = 0.35f;
	constexpr int k_segments = 16;
	const float lower = glm::radians(limit_lower);
	const float upper = glm::radians(limit_upper);
	glm::vec3 previous = origin + (glm::angleAxis(lower, axis) * axes[0] * k_radius);
	debug::drawLine(origin, previous, color);
	for (int segment = 1; segment <= k_segments; ++segment) {
		const float angle = lower + ((upper - lower) * static_cast<float>(segment) / static_cast<float>(k_segments));
		const glm::vec3 point = origin + (glm::angleAxis(angle, axis) * axes[0] * k_radius);
		debug::drawLine(previous, point, color);
		previous = point;
	}
	debug::drawLine(origin, previous, color);
}

}
