/// @file Node3D.hpp
/// @date 5/30/2025
/// @author Xein
/// @brief The simplest Node that exists on the game world

#pragma once

#include "Node.hpp"
#include "Toast/SubNodes/TransformSubNode.hpp"
#include "Toast/Event/ListenerSubNode.hpp"

namespace toast {

class Node3D : public Node {
public:
	REGISTER_TYPE(Node3D);
	Node3D();

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;
#ifdef TOAST_EDITOR
	void Inspector() override;
#endif
	constexpr BaseType base_type() const noexcept final {
		return Node3DT;
	}

protected:
	// Stage functions
	void Init() override;

	void Begin() override { }

	void EarlyTick() override { }

	void Tick() override { }

	void LateTick() override { }

	void PhysTick() override { }

	void Destroy() override { }

	void OnEnable() override { }

	void OnDisable() override { }

public:
	[[nodiscard]]
	TransformSubNode* transform() const;

	[[nodiscard]]
	event::ListenerSubNode* listener() const;

private:
	std::unique_ptr<TransformSubNode> m_transform = nullptr;
	std::unique_ptr<event::ListenerSubNode> m_listener = nullptr;
};

}
