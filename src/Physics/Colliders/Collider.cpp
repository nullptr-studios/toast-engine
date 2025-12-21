#include <Engine/Core/GlmJson.hpp>
#include <Engine/Physics/Colliders/Collider.hpp>
#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

namespace physics {

void ICollider::OnCollisionEnter(EnterCallback&& function) {
	m_onCollisionEnter = function;
}

void ICollider::OnCollisionExit(ExitCallback&& function) {
	m_onCollisionExit = function;
}

bool ICollider::is_colliding() const {
	return !m_collidingStack.empty();
}

bool ICollider::is_colliding_with(unsigned id) const {
	return std::ranges::find(m_collidingStack, id) != m_collidingStack.end();
}

void ICollider::CallOnCollisionEnter(Object* other, const ContactInfo& contact) const {
	if (m_onCollisionEnter) {
		m_onCollisionEnter(other, contact);
	}
}

void ICollider::CallOnCollisionExit(Object* other) const {
	if (m_onCollisionExit) {
		m_onCollisionExit(other);
	}
}

json_t ICollider::Save() const {
	json_t j = toast::Component::Save();

	// store flags as a numeric value (cast to unsigned int for JSON)

	j["flags"] = static_cast<int>(flags);
	j["debug"] = debug;
	j["trigger"] = trigger;
	j["color"] = m_color;

	return j;
}

void ICollider::Load(json_t j, bool force_create) {
	toast::Component::Load(j);

	// read flags as integer and cast back to enum
	if (j.contains("flags")) {
		const auto v = j["flags"].get<int>();
		flags = static_cast<ColliderFlags>(v);
	}

	if (j.contains("debug")) {
		debug = j["debug"].get<bool>();
	}
	if (j.contains("trigger")) {
		trigger = j["trigger"].get<bool>();
	}
	if (j.contains("color")) {
		m_color = j["color"].get<glm::vec4>();
	}
}

#ifdef TOAST_EDITOR
void ICollider::Inspector() {
	// Show base component inspector
	toast::Component::Inspector();

	// Flags as bitmask (allow multiple selections)
	unsigned char cur = static_cast<unsigned char>(flags);

	bool none = (cur == 0);
	bool defaultFlag = (cur & static_cast<unsigned char>(ColliderFlags::Default)) != 0;
	bool groundFlag = (cur & static_cast<unsigned char>(ColliderFlags::Ground)) != 0;
	bool enemyFlag = (cur & static_cast<unsigned char>(ColliderFlags::Enemy)) != 0;
	bool playerFlag = (cur & static_cast<unsigned char>(ColliderFlags::Player)) != 0;

	// None behaves as exclusive: if set, other flags are cleared. Unchecking it leaves the current bits as-is.
	if (ImGui::Checkbox("None", &none)) {
		if (none) {
			cur = 0;
			defaultFlag = groundFlag = enemyFlag = playerFlag = false;
		}
	}

	if (ImGui::Checkbox("Default", &defaultFlag)) {
		if (defaultFlag) {
			cur |= static_cast<unsigned char>(ColliderFlags::Default);
			none = false;
		} else {
			cur &= ~static_cast<unsigned char>(ColliderFlags::Default);
		}
	}
	if (ImGui::Checkbox("Ground", &groundFlag)) {
		if (groundFlag) {
			cur |= static_cast<unsigned char>(ColliderFlags::Ground);
			none = false;
		} else {
			cur &= ~static_cast<unsigned char>(ColliderFlags::Ground);
		}
	}
	if (ImGui::Checkbox("Enemy", &enemyFlag)) {
		if (enemyFlag) {
			cur |= static_cast<unsigned char>(ColliderFlags::Enemy);
			none = false;
		} else {
			cur &= ~static_cast<unsigned char>(ColliderFlags::Enemy);
		}
	}
	if (ImGui::Checkbox("Player", &playerFlag)) {
		if (playerFlag) {
			cur |= static_cast<unsigned char>(ColliderFlags::Player);
			none = false;
		} else {
			cur &= ~static_cast<unsigned char>(ColliderFlags::Player);
		}
	}

	// If none is unchecked and no bits are set, keep none true
	if (!none && cur == 0) {
		none = true;
	}

	flags = static_cast<ColliderFlags>(cur);

	// Color editor
	ImGui::ColorEdit4("Color", &m_color.x);
}
#endif

}
