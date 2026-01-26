/// @file Trigger.hpp
/// @author Xein
/// @date 19 Jan 2026

#pragma once
#include "Toast/Objects/Actor.hpp"

#include <glm/glm.hpp>

namespace toast { class Object; }

namespace physics {

class Rigidbody;

class Trigger : public toast::Actor {
public:
	REGISTER_ABSTRACT(Trigger);

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

	void Begin() override;
	void Destroy() override;
	void EditorTick() override;

	std::list<Rigidbody*> rigidbodies;
	std::function<void(toast::Object*)> enterCallback;
	std::function<void(toast::Object*)> exitCallback;

	virtual void OnEnter(toast::Object*) = 0;
	virtual void OnExit(toast::Object*) = 0;

	struct M {
		glm::vec4 color = {0.0f, 1.0f, 1.0f, 0.5f};
	} m;

	struct {
		bool draw = true;
		bool log = false;
		glm::vec4 defaultColor = {0.0f, 1.0f, 1.0f, 0.5f};
		glm::vec4 collideColor = {1.0f, 0.0f, 0.0f, 0.5f};
	} debug;
};

}
