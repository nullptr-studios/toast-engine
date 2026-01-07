/// @file Component.hpp
/// @author Xein
/// @date 30/05/25
/// @brief Base component class

#pragma once
#include "Toast/Objects/Object.hpp"

namespace toast {

class Actor;

/// @class toast::Component
/// @brief Base Component class
///
/// A component is a type of object that can be attached to an Actor in order
/// to perform some kind of operation. Components are ment to be used instead
/// of just writing that function on the Actor itself when that functionality
/// is used by a number of different Actors. Examples of this could be
/// collision, input, events, AI, health...
///
/// If the code you want to make does not need to be used by multiple unrelated
/// actors consider just adding it to the class you are working on
class Component : public Object {
public:
	REGISTER_ABSTRACT(Component)

	Component() = default;
	~Component() override = default;

	constexpr BaseType base_type() const noexcept final {
		return ComponentT;
	}

protected:
	void Init() override { }

	void Begin() override { }

	void EarlyTick() override { }

	void Tick() override { }

	void LateTick() override { }

	void PhysTick() override { }
};

}
