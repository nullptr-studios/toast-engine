/// @file Actor.hpp
/// @date 5/30/2025
/// @author Xein
/// @brief The simplest Object that exists on the game world

#pragma once

#include "Object.hpp"
#include "Toast/Components/TransformComponent.hpp"
#include "Toast/Event/ListenerComponent.hpp"

namespace toast {

class Actor : public Object {
public:
	REGISTER_TYPE(Actor);
	Actor();

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;
#ifdef TOAST_EDITOR
	void Inspector() override;
#endif
	constexpr BaseType base_type() const noexcept final {
		return ActorT;
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
	TransformComponent* transform() const;

	[[nodiscard]]
	event::ListenerComponent* listener() const;

private:
	std::unique_ptr<TransformComponent> m_transform = nullptr;
	std::unique_ptr<event::ListenerComponent> m_listener = nullptr;
};

}
