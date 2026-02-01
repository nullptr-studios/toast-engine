#include "Toast/Physics/Trigger.hpp"

#include "PhysicsSystem.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"

#define SAVE(name) j[#name] = name;
#define LOAD(name)         \
	if (j.contains(#name)) { \
		name = j[#name];       \
	}

namespace physics {

void Trigger::Begin() {
	Actor::Begin();
	PhysicsSystem::AddTrigger(this);
	enterCallback = [this](Object* o) {
		OnEnter(o);
	};
	exitCallback = [this](Object* o) {
		OnExit(o);
	};
}

void Trigger::Destroy() {
	Actor::Destroy();
	PhysicsSystem::RemoveTrigger(this);
}

json_t Trigger::Save() const {
	json_t j = Actor::Save();
	SAVE(debug.draw);
	SAVE(debug.log);
	SAVE(debug.defaultColor);
	SAVE(debug.collideColor);

	return j;
}

void Trigger::Load(json_t j, bool force_create) {
	LOAD(debug.draw);
	LOAD(debug.log);
	LOAD(debug.defaultColor);
	LOAD(debug.collideColor);

	Actor::Load(j, force_create);
}

void Trigger::EditorTick() {
	if (debug.draw) {
		// TODO: Make this fillable
		renderer::DebugRect(transform()->worldPosition(), transform()->scale(), m.color);
	}
}

}
