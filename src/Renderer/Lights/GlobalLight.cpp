/// @file GlobalLight.cpp
/// @author dario
/// @date 06/12/2025.

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include <Engine/Core/GlmJson.hpp>
#include <Engine/Renderer/IRendererBase.hpp>
#include <Engine/Renderer/Lights/GlobalLight.hpp>

void GlobalLight::Init() {
	Object::Init();

	renderer::IRendererBase::GetInstance()->SetGlobalLightEnabled(m_enableLight);
	renderer::IRendererBase::GetInstance()->SetGlobalLightColor(m_color);
	renderer::IRendererBase::GetInstance()->SetGlobalLightIntensity(m_intensity);
}

void GlobalLight::Load(json_t j, bool force_create) {
	Object::Load(j, force_create);
	if (j.contains("color")) {
		m_color = j.at("color").get<glm::vec3>();
	}
	if (j.contains("intensity")) {
		m_intensity = j.at("intensity").get<float>();
	}
	if (j.contains("enableLight")) {
		m_enableLight = j.at("enableLight").get<bool>();
	}
}

json_t GlobalLight::Save() const {
	json_t j = Object::Save();
	j["color"] = m_color;
	j["intensity"] = m_intensity;
	j["enableLight"] = m_enableLight;
	return j;
}

#ifdef TOAST_EDITOR
void GlobalLight::Inspector() {
	Object::Inspector();
	if (ImGui::Checkbox("Enable Global Light", &m_enableLight)) {
		renderer::IRendererBase::GetInstance()->SetGlobalLightEnabled(m_enableLight);
	}
	if (ImGui::SliderFloat("Light Intensity", &m_intensity, 0.0, 3.0f)) {
		renderer::IRendererBase::GetInstance()->SetGlobalLightIntensity(m_intensity);
	}
	if (ImGui::ColorEdit3("Light Color", &m_color.r)) {
		renderer::IRendererBase::GetInstance()->SetGlobalLightColor(m_color);
	}
}
#endif
