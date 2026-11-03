#include "pin_joint.hpp"

#include <toast/renderer/debug/debug_draw.hpp>

namespace physics {

void PinJoint::drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const {
	debug::drawSphere(origin, 0.05f, color, 12);
}

}
