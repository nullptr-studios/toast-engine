/**
 * @file light.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "node_3d.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Hidden, Icon("PointLight")]] TOAST_API Light : public Node3D {
public:
	Light() = default;

private:
	[[Reflect, Color]]
	glm::vec3 m_light_color = glm::vec3(1.0f, 1.0f, 1.0f);

	[[Reflect, Unit("lm")]]
	float m_intensity = 1.0f;
};
}
