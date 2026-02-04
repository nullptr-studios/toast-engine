/// @file AtlasSpriteComponent.cpp
/// @author dario
/// @date 04/02/2026.

#include "Toast/Components/AtlasSpriteComponent.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

namespace toast {

void AtlasSpriteComponent::Init() {
	TransformComponent::Init();
}

void AtlasSpriteComponent::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	
	if (j.contains("regionName")) {
		m_regionName = j.at("regionName").get<std::string>();
	}
	
	if (j.contains("color")) {
		auto& colorArr = j.at("color");
		if (colorArr.is_array() && colorArr.size() >= 4) {
			m_color.r = colorArr[0].get<float>();
			m_color.g = colorArr[1].get<float>();
			m_color.b = colorArr[2].get<float>();
			m_color.a = colorArr[3].get<float>();
		}
	}
}

json_t AtlasSpriteComponent::Save() const {
	json_t j = TransformComponent::Save();
	j["regionName"] = m_regionName;
	j["color"] = { m_color.r, m_color.g, m_color.b, m_color.a };
	return j;
}

#ifdef TOAST_EDITOR

void AtlasSpriteComponent::Inspector() {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		TransformComponent::Inspector();
		ImGui::Unindent(20);
	}
	
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Atlas Sprite");
	
	ImGui::Text("Region: %s", m_regionName.empty() ? "<none>" : m_regionName.c_str());
	
	// Color picker
	float color[4] = { m_color.r, m_color.g, m_color.b, m_color.a };
	if (ImGui::ColorEdit4("Color", color)) {
		m_color.r = color[0];
		m_color.g = color[1];
		m_color.b = color[2];
		m_color.a = color[3];
	}
}

#endif

}
