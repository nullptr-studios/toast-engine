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
	Node3D::Begin();
	PhysicsSystem::AddTrigger(this);
	enterCallback = [this](Node* o) {
		if (!enabled()) {
			return;
		}
		OnEnter(o);
	};
	exitCallback = [this](Node* o) {
		if (!enabled()) {
			return;
		}
		OnExit(o);
	};
}

void Trigger::Destroy() {
	Node3D::Destroy();
	PhysicsSystem::RemoveTrigger(this);
}

json_t Trigger::Save() const {
	json_t j = Node3D::Save();
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

	Node3D::Load(j, force_create);
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
