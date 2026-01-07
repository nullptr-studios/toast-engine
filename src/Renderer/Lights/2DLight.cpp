/// @file 2DLight.cpp
/// @author dario
/// @date 22/11/2025.

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include "Toast/Renderer/Lights/2DLight.hpp"

void Light2D::Init() {
	// Load quad mesh for light rendering
	m_lightMesh = resource::ResourceManager::GetInstance()->LoadResource<renderer::Mesh>("assets/models/quad.obj");
	m_lightShader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>("assets/shaders/2dLight.shader");

	transform()->scale(glm::vec3(m_radius * 2, m_radius * 2, 1.0f));
	renderer::IRendererBase::GetInstance()->AddLight(this);
	m_lightBuffer = renderer::IRendererBase::GetInstance()->GetLightFramebuffer();
}

void Light2D::Begin() { }

void Light2D::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveLight(this);
}

void Light2D::OnRender(const glm::mat4& premultiplied_matrix) const {
	auto model = transform()->GetWorldMatrix();
	auto mvp = premultiplied_matrix * model;

	m_lightShader->Use();

	// famebuffer samplers
	m_lightShader->SetSampler("gLightAccum", 0);
	m_lightShader->SetSampler("gNormal", 1);

	m_lightShader->Set("gMVP", mvp);
	m_lightShader->Set("gLightColor", m_color);
	m_lightShader->Set("gLightIntensity", m_intensity);
	m_lightShader->Set("gLightVolumetricIntensity", m_volumetricIntensity);

	m_lightShader->Set("gLightAngle", glm::radians(m_angle));

	m_lightShader->Set("gRadialSoftness", m_radialSoftness);
	m_lightShader->Set("gAngularSoftness", m_angularSoftness);

	m_lightShader->Set("gNormalMappingEnabled", m_normalMappingEnabled);

	m_lightShader->Set(
	    "gInvScreenSize", glm::vec2(1.0f / static_cast<float>(m_lightBuffer->Width()), 1.0f / static_cast<float>(m_lightBuffer->Height()))
	);

	// Bind current light accumulation texture
	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, m_lightBuffer->GetColorTexture(0));

	// Normal
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, m_lightBuffer->GetColorTexture(1));

	m_lightMesh->Draw();

	glActiveTexture(GL_TEXTURE0);
}

json_t Light2D::Save() const {
	json_t j = Actor::Save();
	j["radius"] = m_radius;
	j["intensity"] = m_intensity;
	j["volumetric_intensity"] = m_volumetricIntensity;
	j["angle"] = m_angle;
	j["radial_softness"] = m_radialSoftness;
	j["angular_softness"] = m_angularSoftness;
	j["normal_mapping_enabled"] = m_normalMappingEnabled;
	j["color"] = { m_color.r, m_color.g, m_color.b, m_color.a };
	return j;
}

void Light2D::Load(json_t j, bool force_create) {
	Actor::Load(j, force_create);
	if (j.contains("radius")) {
		m_radius = j.at("radius").get<float>();
	}
	if (j.contains("intensity")) {
		m_intensity = j.at("intensity").get<float>();
	}
	if (j.contains("volumetric_intensity")) {
		m_volumetricIntensity = j.at("volumetric_intensity").get<float>();
	}
	if (j.contains("angle")) {
		m_angle = j.at("angle").get<float>();
	}
	if (j.contains("radial_softness")) {
		m_radialSoftness = j.at("radial_softness").get<float>();
	}
	if (j.contains("angular_softness")) {
		m_angularSoftness = j.at("angular_softness").get<float>();
	}
	if (j.contains("normal_mapping_enabled")) {
		m_normalMappingEnabled = j.at("normal_mapping_enabled").get<bool>();
	}
	if (j.contains("color")) {
		auto color_array = j.at("color").get<std::vector<float>>();
		if (color_array.size() == 4) {
			m_color = glm::vec4(color_array[0], color_array[1], color_array[2], color_array[3]);
		}
	}
}

#ifdef TOAST_EDITOR
void Light2D::Inspector() {
	Actor::Inspector();

	ImGui::DragFloat("Light Radius", &m_radius, 0.5f, 0.0f, 10000.0f);
	ImGui::DragFloat("Light Intensity", &m_intensity, 0.05f, 0.0f, 1.0f);
	ImGui::DragFloat("Light Volumetric Intensity", &m_volumetricIntensity, 0.05f, 0.0f, 1.0f);
	ImGui::DragFloat("Light Angle", &m_angle, 1.0f, 0.0f, 180.0f);
	ImGui::DragFloat("Radial Softness", &m_radialSoftness, 0.01f, 0.001f, .25f);
	ImGui::DragFloat("Angular Softness", &m_angularSoftness, 0.01f, 0.000f, 1.0f);

	ImGui::ColorEdit4("Light Color", &m_color.r);

	ImGui::Separator();
	ImGui::Checkbox("Enable Normal Mapping", &m_normalMappingEnabled);

	transform()->scale(glm::vec3(m_radius * 2, m_radius * 2, 1.0f));
}
#endif
