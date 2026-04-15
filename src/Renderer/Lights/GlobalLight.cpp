/// @file GlobalLight.cpp
/// @author dario
/// @date 06/12/2025.

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Toast/GlmJson.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/Lights/GlobalLight.hpp"

#include <algorithm>

namespace {
glm::vec3 ResolveLightDirection(GlobalLight* light) {
	if (!light || !light->transform()) {
		return glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f));
	}
	const glm::vec3 forward = light->transform()->GetFrontVector();
	const glm::vec3 candidate = -forward;
	const float len2 = glm::dot(candidate, candidate);
	if (len2 <= 1e-6f) {
		return glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f));
	}
	return candidate / glm::sqrt(len2);
}

glm::vec3 NormalizeSafe(const glm::vec3& direction) {
	const float len2 = glm::dot(direction, direction);
	if (len2 <= 1e-6f) {
		return glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f));
	}
	return direction / glm::sqrt(len2);
}
}

void GlobalLight::Init() {
	Actor::Init();

	renderer::IRendererBase::GetInstance()->SetGlobalLightEnabled(m_enableLight);
	renderer::IRendererBase::GetInstance()->SetGlobalLightColor(m_color);
	renderer::IRendererBase::GetInstance()->SetGlobalLightIntensity(m_intensity);
	renderer::IRendererBase::GetInstance()->SetGlobalLightDirection(m_useTransformDirection ? ResolveLightDirection(this) : m_manualDirection);
	renderer::IRendererBase::GetInstance()->SetDirectionalShadowsEnabled(m_enableLight);
}

void GlobalLight::Tick() {
	Object::Tick();
	auto* renderer = renderer::IRendererBase::GetInstance();
	if (!renderer) {
		return;
	}
	renderer->SetGlobalLightDirection(m_useTransformDirection ? ResolveLightDirection(this) : m_manualDirection);
	renderer->SetDirectionalShadowsEnabled(m_enableLight);
	
}

void GlobalLight::Load(json_t j, bool force_create) {
	Actor::Load(j, force_create);
	if (j.contains("color")) {
		m_color = j.at("color").get<glm::vec3>();
	}
	if (j.contains("intensity")) {
		m_intensity = j.at("intensity").get<float>();
	}
	if (j.contains("enableLight")) {
		m_enableLight = j.at("enableLight").get<bool>();
	}
	if (j.contains("useTransformDirection")) {
		m_useTransformDirection = j.at("useTransformDirection").get<bool>();
	}
	if (j.contains("manualDirection")) {
		m_manualDirection = NormalizeSafe(j.at("manualDirection").get<glm::vec3>());
	}
	if (j.contains("showDirectionalShadowArrow")) {
		m_showDirectionalShadowArrow = j.at("showDirectionalShadowArrow").get<bool>();
	}
	if (j.contains("directionalShadowArrowLength")) {
		m_directionalShadowArrowLength = std::max(0.1f, j.at("directionalShadowArrowLength").get<float>());
	}
}

json_t GlobalLight::Save() const {
	json_t j = Actor::Save();
	j["color"] = m_color;
	j["intensity"] = m_intensity;
	j["enableLight"] = m_enableLight;
	j["useTransformDirection"] = m_useTransformDirection;
	j["manualDirection"] = m_manualDirection;
	j["showDirectionalShadowArrow"] = m_showDirectionalShadowArrow;
	j["directionalShadowArrowLength"] = m_directionalShadowArrowLength;
	return j;
}

#ifdef TOAST_EDITOR
void GlobalLight::Inspector() {
	Actor::Inspector();
	if (ImGui::Checkbox("Enable Global Light", &m_enableLight)) {
		renderer::IRendererBase::GetInstance()->SetGlobalLightEnabled(m_enableLight);
	}
	if (ImGui::SliderFloat("Light Intensity", &m_intensity, 0.0, 3.0f)) {
		renderer::IRendererBase::GetInstance()->SetGlobalLightIntensity(m_intensity);
	}
	if (ImGui::ColorEdit3("Light Color", &m_color.r)) {
		renderer::IRendererBase::GetInstance()->SetGlobalLightColor(m_color);
	}
	if (ImGui::Checkbox("Direction From Transform", &m_useTransformDirection)) {
		renderer::IRendererBase::GetInstance()->SetGlobalLightDirection(m_useTransformDirection ? ResolveLightDirection(this) : m_manualDirection);
	}
	if (!m_useTransformDirection) {
		if (ImGui::DragFloat3("Directional Light Dir", &m_manualDirection.x, 0.01f, -1.0f, 1.0f)) {
			m_manualDirection = NormalizeSafe(m_manualDirection);
			renderer::IRendererBase::GetInstance()->SetGlobalLightDirection(m_manualDirection);
		}
	}
	ImGui::Checkbox("Show Shadow Direction Arrow", &m_showDirectionalShadowArrow);
	ImGui::DragFloat("Shadow Arrow Length", &m_directionalShadowArrowLength, 0.1f, 0.1f, 50.0f);
	m_directionalShadowArrowLength = std::max(0.1f, m_directionalShadowArrowLength);
	renderer::IRendererBase::GetInstance()->SetGlobalLightDirection(m_useTransformDirection ? ResolveLightDirection(this) : m_manualDirection);
	if (m_showDirectionalShadowArrow && enabled() && renderer::DebugDrawLayer::GetInstance() && transform()) {
		renderer::DebugArrow3D(
				transform()->worldPosition(),
				m_useTransformDirection ? ResolveLightDirection(this) : m_manualDirection,
				m_directionalShadowArrowLength,
				glm::vec4(1.0f, 0.8f, 0.2f, 1.0f)
		);
	}
}
#endif
