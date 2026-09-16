/**
 * @file CapsuleCollider.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "collider.hpp"

namespace physics {

class [[ToastNode, Icon("CapsuleMesh")]] TOAST_API CapsuleCollider : public physics::Collider {
public:
	[[Reflect, Unit("m")]]
	float radius = 0.5f;
	[[Reflect, Unit("m")]]
	float height = 2.0f;

private:
};

}
