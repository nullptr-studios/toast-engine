/**
 * @file BoxCollider.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "collider.hpp"

namespace physics {

class [[ToastNode, Icon("BoxMesh")]] TOAST_API BoxCollider : public physics::Collider {
public:
	[[Reflect, Unit("m")]]
	glm::vec3 size = glm::vec3(1.0f);

private:
};

}
