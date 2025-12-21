#include "../PhysicsSystem.hpp"

#include <Engine/Core/GlmJson.hpp>
#include <Engine/Physics/Colliders/BoxCollider.hpp>
#include <Engine/Renderer/DebugDrawLayer.hpp>
#include <Engine/Toast/Objects/Actor.hpp>
#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

namespace physics {

void BoxCollider::Begin() {
	ICollider::Begin();
	AddCollider(this);
}

void BoxCollider::EditorTick() {
	if (debug) {
		glm::vec3 size3 = static_cast<toast::Actor*>(parent())->transform()->worldScale();
		glm::vec2 size = { size3.x, size3.y };
		renderer::DebugRect(GetPosition(), m_size * size, color());
	}
}

void BoxCollider::Destroy() {
	RemoveCollider(this);
}

json_t BoxCollider::Save() const {
	json_t j = ICollider::Save();
	j["offset"] = m_position;
	j["size"] = m_size;
	j["angle"] = m_angle;
	j["debug"] = debug;
	j["trigger"] = trigger;
	return j;
}

void BoxCollider::Load(json_t j, bool force_create) {
	ICollider::Load(j);
	if (j.contains("offset")) {
		m_position = j["offset"].get<glm::vec2>();
	}
	if (j.contains("size")) {
		m_size = j["size"].get<glm::vec2>();
	}
	if (j.contains("angle")) {
		m_angle = j["angle"].get<int>();
	}
	if (j.contains("debug")) {
		debug = j["debug"].get<bool>();
	}
	if (j.contains("trigger")) {
		trigger = j["trigger"].get<bool>();
	}
}

#ifdef TOAST_EDITOR
void BoxCollider::Inspector() {
	ICollider::Inspector();
	ImGui::DragFloat2("Offset", &m_position.x, 0.1f);
	ImGui::DragFloat2("Size", &m_size.x, 0.1f);
	ImGui::DragFloat("Angle", &m_angle, 0.1f);
	ImGui::Spacing();
	ImGui::Checkbox("Is Trigger", &trigger);
	ImGui::Spacing();
	ImGui::Checkbox("Debug", &debug);
}
#endif

void BoxCollider::SetSize(const glm::vec2 size) {
	m_size = size;
}

void BoxCollider::SetPosition(const glm::vec2 position) {
	m_position = position;
}

void BoxCollider::SetRotation(const float rotation) {
	m_angle = rotation;
}

glm::vec2 BoxCollider::GetSize(bool local) const {
	if (local) {
		return m_size;
	}

	if (parent()->base_type() != toast::ActorT) {
		return m_size;
	}
	auto* parent_transform = static_cast<toast::Actor*>(parent())->transform();

	const glm::vec3 world_scale = parent_transform->worldScale();
	const glm::vec2 scale(world_scale.x, world_scale.y);
	return m_size * scale;
}

glm::vec2 BoxCollider::GetPosition(bool local) const {
	if (local) {
		return m_position;
	}

	if (parent()->base_type() != toast::ActorT) {
		return m_position;
	}
	auto* parent_transform = static_cast<toast::Actor*>(parent())->transform();
	const glm::vec2 position = parent_transform->worldPosition();
	return m_position + position;
}

float BoxCollider::GetRotation(bool local) const {
	if (local) {
		return m_angle;
	}

	if (parent()->base_type() != toast::ActorT) {
		return m_angle;
	}
	auto* parent_transform = static_cast<toast::Actor*>(parent())->transform();
	return m_angle + parent_transform->worldRotationRadians().z;
}

}
