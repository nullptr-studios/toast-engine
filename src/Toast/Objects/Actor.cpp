#include "Toast/Objects/Object.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Toast/Components/TransformComponent.hpp"
#include "Toast/Event/ListenerComponent.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Objects/Actor.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"

namespace toast {

Actor::Actor() {
	// Adds a transform to the Actor
	m_transform = std::make_unique<TransformComponent>();
	m_transform->m_id = Factory::AssignId();
	m_transform->SetAttachedActor(this);
	m_transform->m_parent = this;
	// Adds an Event Listener component
	m_listener = std::make_unique<event::ListenerComponent>();
}

json_t Actor::Save() const {
	PROFILE_ZONE_C(0x00FF00);  // Green for serialization
	json_t j = Object::Save();
	json_t transform_j;
	transform_j["position"] = m_transform->position();
	transform_j["rotation"] = m_transform->rotationQuat();
	transform_j["scale"] = m_transform->scale();
	j["transform"] = transform_j;
	return j;
}

void Actor::Load(json_t j, bool force_create) {
	PROFILE_ZONE_C(0x00FFFF);  // Cyan for deserialization

	auto transform_j = j["transform"];
	m_transform->position(transform_j["position"].get<glm::vec3>());
	m_transform->rotationQuat(transform_j["rotation"].get<glm::quat>());
	m_transform->scale(transform_j["scale"].get<glm::vec3>());

	Object::Load(j, force_create);
}

#ifdef TOAST_EDITOR
void Actor::Inspector() {
	Object::Inspector();
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		transform()->Inspector();
		ImGui::Unindent(20);
	}
}
#endif

void Actor::Init() { }

TransformComponent* Actor::transform() const {
	if (!m_transform) {
		throw ToastException("No transform provided in actor???");
	}
	return m_transform.get();
}

event::ListenerComponent* Actor::listener() const {
	if (!m_listener) {
		throw ToastException("No listener provided in actor???");
	}
	return m_listener.get();
}

}
