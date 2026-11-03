#include "fixed_joint.hpp"

#include <toast/renderer/debug/debug_draw.hpp>

namespace physics {

void FixedJoint::drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const {
	constexpr float k_half_size = 0.06f;
	debug::drawBox(origin - glm::vec3(k_half_size), origin + glm::vec3(k_half_size), color);
}

}
