#include "levitation_joint.hpp"

#include <algorithm>
#include <glm/gtc/constants.hpp>
#include <toast/renderer/debug/debug_draw.hpp>

namespace physics {

namespace {

void drawCircle(const glm::vec3& origin, const glm::vec3& first, const glm::vec3& second, float radius, const glm::vec4& color) {
	constexpr int k_segments = 24;
	glm::vec3 previous = origin + (first * radius);
	for (int segment = 1; segment <= k_segments; ++segment) {
		const float angle = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(k_segments);
		const glm::vec3 point = origin + (((first * std::cos(angle)) + (second * std::sin(angle))) * radius);
		debug::drawLine(previous, point, color);
		previous = point;
	}
}

}

auto LevitationJoint::makeParams() const -> JointParams {
	return LevitationParams {
	  .stiffness_hz = stiffness_hz,
	  .damping_ratio = damping_ratio,
	  .max_force = std::max(max_force, 0.0f),
	  .catch_radius = catch_radius,
	  .release_radius = std::max(release_radius, catch_radius),
	  .rotation_free = glm::bvec3(rotation_free_x, rotation_free_y, rotation_free_z),
	  .angular_damping = angular_damping,
	};
}

void LevitationJoint::applyState(const JointState& state) {
	current_offset = state.offset;
	if (levitating == state.levitating) {
		return;
	}
	levitating = state.levitating;
	if (levitating) {
		captured.fire();
	} else {
		released.fire();
	}
}

void LevitationJoint::updateInspectorMessages() {
	Joint3D::updateInspectorMessages();

	static const toast::NodeMessage radii_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 16,
	  .text = "The catch radius is bigger than the release radius, the release radius is used as big as the catch radius",
	};
	if (catch_radius > release_radius) {
		addInspectorMessage(radii_message);
	} else {
		removeInspectorMessage(radii_message);
	}
}

void LevitationJoint::drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const {
	const glm::vec4 inner {color.r, color.g, color.b, 0.35f};
	const float outer_radius = std::max(release_radius, catch_radius);
	for (int index = 0; index < 3; ++index) {
		const glm::vec3& first = axes[index];
		const glm::vec3& second = axes[(index + 1) % 3];
		drawCircle(origin, first, second, catch_radius, inner);
		drawCircle(origin, first, second, outer_radius, color);
	}
}

}
