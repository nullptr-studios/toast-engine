#include "../PhysicsSystem.hpp"

#include <Engine/Core/GlmJson.hpp>
#include <Engine/Physics/Colliders/CircleCollider.hpp>
#include <Engine/Renderer/DebugDrawLayer.hpp>
#include <Engine/Toast/Objects/Actor.hpp>
#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

namespace physics {

void CircleCollider::Begin() {
	ICollider::Begin();
	AddCollider(this);
}

void CircleCollider::EditorTick() {
#ifndef NDEBUG
	if (debug) {
		const auto* parent_transform = dynamic_cast<toast::Actor*>(parent())->transform();
		const glm::vec2 position = { parent_transform->position().x + m_position.x, parent_transform->position().y + m_position.y };
		renderer::DebugCircle(position, m_radius, color());
	}
#endif
}

void CircleCollider::Destroy() {
	RemoveCollider(this);
}

#pragma region serialize

json_t CircleCollider::Save() const {
	json_t j = ICollider::Save();

	j["offset"] = m_position;
	j["radius"] = m_radius;
	j["debug"] = debug;
	j["trigger"] = trigger;
	return j;
}

void CircleCollider::Load(json_t j, bool force_create) {
	ICollider::Load(j);
	try {
		m_position = j["offset"].get<glm::vec2>();
		m_radius = j["radius"].get<float>();
		debug = j["debug"].get<bool>();
		trigger = j["trigger"].get<bool>();
	} catch (nlohmann::detail::parse_error& e) { TOAST_WARN("Failed to parse json for {}: {}", name(), e.what()); }
}

#ifdef TOAST_EDITOR
void CircleCollider::Inspector() {
	ICollider::Inspector();
	ImGui::InputFloat2("Position", &m_position.x);
	ImGui::InputFloat2("Radius", &m_radius);
	ImGui::Spacing();
	ImGui::Checkbox("Is Trigger", &trigger);
	ImGui::Spacing();
	ImGui::Checkbox("Debug", &debug);
}
#endif

#pragma endregion

void CircleCollider::SetPosition(const glm::vec2 position) {
	m_position = position;
}

void CircleCollider::SetRadius(const float radius) {
	m_radius = radius;
}

float CircleCollider::GetRadius(bool local) const {
	if (local) {
		return m_radius;
	}

	if (parent()->base_type() != toast::ActorT) {
		return m_radius;
	}
	auto* actor = static_cast<toast::Actor*>(parent());
	const glm::vec3 s = actor->transform()->worldScale();
	const float scale_2d = std::max(std::abs(s.x), std::abs(s.y));
	return m_radius * scale_2d;
}

glm::vec2 CircleCollider::GetPosition(bool local) const {
	if (local) {
		return m_position;
	}

	if (parent()->base_type() != toast::ActorT) {
		return m_position;
	}
	auto* parent_transform = static_cast<toast::Actor*>(parent())->transform();
	glm::vec2 position = parent_transform->worldPosition();
	return m_position + position;
}

}
