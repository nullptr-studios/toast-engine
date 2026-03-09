/// @file SubNode.hpp
/// @author Xein
/// @date 30/05/25
/// @brief Base component class

#pragma once
#include "Toast/Nodes/Node.hpp"

namespace toast {

class Node3D;

/// @class toast::SubNode
/// @brief Base SubNode class
///
/// A component is a type of object that can be attached to an Node3D in order
/// to perform some kind of operation. SubNodes are ment to be used instead
/// of just writing that function on the Node3D itself when that functionality
/// is used by a number of different Node3Ds. Examples of this could be
/// collision, input, events, AI, health...
///
/// If the code you want to make does not need to be used by multiple unrelated
/// actors consider just adding it to the class you are working on
class SubNode : public Node {
public:
	REGISTER_ABSTRACT(SubNode)

	SubNode() = default;
	~SubNode() override = default;

	constexpr BaseType base_type() const noexcept final {
		return SubNodeT;
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
