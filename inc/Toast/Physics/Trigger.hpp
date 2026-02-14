/// @file Trigger.hpp
/// @author Xein
/// @date 19 Jan 2026

#pragma once
#include "Toast/Objects/Actor.hpp"

#include "ColliderFlags.hpp"
#include "Toast/Physics/ColliderFlags.hpp"

#include <glm/glm.hpp>

namespace toast {
class Object;
}

namespace physics {

class Rigidbody;

class Trigger : public toast::Actor {
public:
	REGISTER_TYPE(Trigger);

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

	void Begin() override;
	void Destroy() override;
	void EditorTick() override;

	virtual void OnEnter(toast::Object*) { }
	virtual void OnExit(toast::Object*) { }

	void AddFlag(ColliderFlags flag);
	void RemoveFlag(ColliderFlags flag);
	const ColliderFlags& flags = m.flags;

	const std::function<void(toast::Object*)>& enterCallback = m.enterCallback;
	const std::function<void(toast::Object*)>& exitCallback = m.exitCallback;
	std::list<Rigidbody*> rigidbodies; ///< Used to avoid a rigidbody triggering twice

	struct M {
		glm::vec4 color = { 0.0f, 1.0f, 1.0f, 0.5f };
		std::function<void(toast::Object*)> enterCallback;
		std::function<void(toast::Object*)> exitCallback;
		ColliderFlags flags = ColliderFlags::Player; ///< Triggers react only to players by default
	} m;

	struct {
		bool draw = true;
		bool log = false;
		glm::vec4 defaultColor = { 0.0f, 1.0f, 1.0f, 0.5f };
		glm::vec4 collideColor = { 1.0f, 0.0f, 0.0f, 0.5f };
	} debug;
};

}
