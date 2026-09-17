/**
 * @file StaticRigidbody.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "rigidbody.hpp"

namespace physics {

class [[ToastNode]] TOAST_API StaticRigidbody : public physics::Rigidbody {
public:
	StaticRigidbody() : Rigidbody(BodyType::static_body) { }

	[[Reflect, Units("m/s"), Name("Constant Linear Velocity")]]
	glm::vec3 linear_velocity = {};

	[[Reflect, Units("rad/s"), Name("Constant Angular Velocity")]]
	glm::vec3 angular_velocity = {};

private:
};

}
