/**
 * @file KinematicRigidbody.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "rigidbody.hpp"

namespace physics {

class [[ToastNode, Icon("CharacterBody")]] TOAST_API KinematicRigidbody : public physics::Rigidbody {
public:
	KinematicRigidbody() : Rigidbody(BodyType::kinematic_body) { }

private:
};

}
