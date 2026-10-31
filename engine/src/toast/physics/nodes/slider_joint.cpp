#include "slider_joint.hpp"

#include <toast/renderer/debug/debug_draw.hpp>

namespace physics {

void SliderJoint::drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const {
	// The rail it runs on with a stop at each end
	const glm::vec3 axis = axes[0];
	const glm::vec3 start = origin + (axis * linear_limit_lower);
	const glm::vec3 end = origin + (axis * linear_limit_upper);
	debug::drawLine(start, end, color);
	for (const glm::vec3& stop : {start, end}) {
		debug::drawLine(stop - (axes[1] * 0.05f), stop + (axes[1] * 0.05f), color);
		debug::drawLine(stop - (axes[2] * 0.05f), stop + (axes[2] * 0.05f), color);
	}
}

}
