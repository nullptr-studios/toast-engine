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
	friend class Simulator;

public:
	KinematicRigidbody() : Rigidbody(BodyType::kinematic_body) { }

protected:
	/// Runs once per fixed step before integration so a controller can move its body
	virtual void simulate(Simulator& simulator, float dt) { }
};

}
