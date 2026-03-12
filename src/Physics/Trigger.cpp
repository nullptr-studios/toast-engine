#include "Toast/Physics/Trigger.hpp"

#include "PhysicsSystem.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/World.hpp"

#define SAVE(name) j[#name] = name;
#define LOAD(name)         \
	if (j.contains(#name)) { \
		name = j[#name];       \
	}

namespace physics {

void Trigger::Init() {
	if (toast::World::IsRunning()) {
		enabled_ref() = false; // disable colliders until its loaded
	}
}

void Trigger::Begin() {
	Actor::Begin();
	enterCallback = [this](Object* o) {
		if (!enabled()) {
			return;
		}
		OnEnter(o);
	};
	exitCallback = [this](Object* o) {
		if (!enabled()) {
			return;
		}
		OnExit(o);
	};
}

void Trigger::OnEnable() {
	TOAST_TRACE("[PHYSICS SYSTEM] Added trigger {}", name());
	PhysicsSystem::AddTrigger(this);
}

void Trigger::OnDisable() {
	TOAST_TRACE("[PHYSICS SYSTEM] Removed trigger {}", name());
	PhysicsSystem::RemoveTrigger(this);
}

void Trigger::Destroy() {
	TOAST_TRACE("[PHYSICS SYSTEM] Removed trigger {}", name());
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

void Trigger::AddFlag(ColliderFlags flag) {
	if (!(m.flags & flag)) {
		m.flags |= flag;
	}
}

void Trigger::RemoveFlag(ColliderFlags flag) {
	m.flags &= ~flag;
}

#ifdef TOAST_EDITOR
void Trigger::EditorTick() {
	if (!enabled()) {
		return;
	}
	if (debug.draw) {
		// TODO: Make this fillable
		renderer::DebugRect(transform()->worldPosition(), transform()->scale(), m.color);
	}
}
#endif

}
