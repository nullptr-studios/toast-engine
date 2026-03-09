/// @file Trigger.hpp
/// @author Xein
/// @date 19 Jan 2026

#pragma once
#include "ColliderFlags.hpp"
#include "Toast/Nodes/Node3D.hpp"
#include "Toast/Physics/ColliderFlags.hpp"

#include <glm/glm.hpp>

namespace toast {
class Node;
}

namespace physics {

class Rigidbody;

class Trigger : public toast::Node3D {
public:
	REGISTER_TYPE(Trigger);

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

	void Begin() override;
	void Destroy() override;
#ifdef TOAST_EDITOR
	void EditorTick() override;
#endif

	virtual void OnEnter(toast::Node*) { }

	virtual void OnExit(toast::Node*) { }

	void AddFlag(ColliderFlags flag);
	void RemoveFlag(ColliderFlags flag);
	const ColliderFlags& flags = m.flags;

	// This needs to be public to be able to override without needing to create
	// a child class -- OnEnter and OnExit will stop working tho -x
	std::function<void(toast::Node*)> enterCallback;
	std::function<void(toast::Node*)> exitCallback;
	std::list<Rigidbody*> rigidbodies;    ///< Used to avoid a rigidbody triggering twice

	struct M {
		glm::vec4 color = { 0.0f, 1.0f, 1.0f, 0.5f };
		ColliderFlags flags = ColliderFlags::Player;    ///< Triggers react only to players by default
	} m;

	struct {
		bool draw = true;
		bool log = false;
		glm::vec4 defaultColor = { 0.0f, 1.0f, 1.0f, 0.5f };
		glm::vec4 collideColor = { 1.0f, 0.0f, 0.0f, 0.5f };
	} debug;
};

}
