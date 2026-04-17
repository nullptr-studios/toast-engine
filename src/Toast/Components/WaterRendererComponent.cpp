/// @file WaterRendererComponent.cpp

#include "Toast/Components/WaterRendererComponent.hpp"

#include "Toast/Log.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Time.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

namespace {

std::string ResolveWaterShaderPath() {
	constexpr const char* kWaterShader = "SHADERS/water_refraction.shader";
	constexpr const char* kWaterShaderLower = "shaders/water_refraction.shader";
	if (resource::Open(kWaterShader).has_value()) {
		return kWaterShader;
	}
	if (resource::Open(kWaterShaderLower).has_value()) {
		return kWaterShaderLower;
	}
	return "SHADERS/default.shader";
}

}

namespace toast {

void WaterRendererComponent::Init() {
	SetTransparent(true);
	SetUseExternalTextureOnly(false);
	MeshRendererComponent::Init();

	m_waterShader = resource::LoadResource<renderer::Shader>(ResolveWaterShaderPath());
	m_normalTexture = resource::LoadResource<Texture>("EDITOR/WaterNormal.jpg");
}

void WaterRendererComponent::LoadTextures() {
	MeshRendererComponent::LoadTextures();
	if (!m_normalTexture) {
		m_normalTexture = resource::LoadResource<Texture>("EDITOR/WaterNormal.jpg");
	}
}

void WaterRendererComponent::OnRender(renderer::IRenderablePass pass, const glm::mat4& precomputed_mat) noexcept {
	if (pass != renderer::IRenderablePass::GEOMETRY || !enabled()) {
		return;
	}

	auto mesh = GetMesh().lock();
	if (!mesh || !m_waterShader || m_refractionTextureId == 0) {
		return;
	}

	if (!OclussionVolume::isTransformedAABBOnPlanes(mesh->boundingBox(), GetWorldMatrix())) {
		return;
	}

	if (!m_normalTexture || m_normalTexture->id() == 0) {
		if (!m_normalTexture) {
			TOAST_WARN("WaterRendererComponent: missing normal texture EDITOR/WaterNormal.jpg");
		}
		return;
	}

	const glm::mat4 model = GetWorldMatrix();
	const glm::mat4 mvp = precomputed_mat * model;

	m_waterShader->Use();
	m_waterShader->Set("gWorld", model);
	m_waterShader->Set("gMVP", mvp);
	m_waterShader->Set("gColor", m_tint);
	m_waterShader->Set("uRefractionStrengthX", m_refractionStrengthX);
	m_waterShader->Set("uRefractionStrengthY", m_refractionStrengthY);
	m_waterShader->Set("uBaseOffsetX", m_baseOffsetX);
	m_waterShader->Set("uBaseOffsetY", m_baseOffsetY);
	m_waterShader->Set("uNormalStrength", m_normalStrength);
	m_waterShader->Set("uNormalTiling", m_normalTiling);
	m_waterShader->Set("uOpacity", m_opacity);
	m_waterShader->Set("uTintMix", m_tintMix);
	m_waterShader->Set("uFlipY", m_flipSceneY ? 1.0f : 0.0f);
	m_waterShader->Set("uWaveSpeed1", m_waveSpeed1);
	m_waterShader->Set("uWaveSpeed2", m_waveSpeed2);
	m_waterShader->Set("uDistortionMix", m_distortionMix);
	m_waterShader->Set("uStraightDistortStrength", m_straightDistortStrength);
	m_waterShader->Set("uStraightBlend", m_straightBlend);
	m_waterShader->Set("uVerticalPull", m_verticalPull);
	m_waterShader->Set("uCausticStrength", m_causticStrength);
	m_waterShader->Set("uCausticScale", m_causticScale);
	m_waterShader->Set("uCausticSpeed", m_causticSpeed);
	m_waterShader->Set("uCausticSharpness", m_causticSharpness);
	m_waterShader->Set("time", static_cast<float>(Time::uptime()));

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_refractionTextureId);
	m_waterShader->SetSampler("uSceneTex", 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_normalTexture->id());
	m_waterShader->SetSampler("uNormalMap", 1);


	mesh->Draw();
}

void WaterRendererComponent::Load(json_t j, bool force_create) {
	MeshRendererComponent::Load(j, force_create);

	if (j.contains("refractionStrength")) {
		const float legacy = j.at("refractionStrength").get<float>();
		m_refractionStrengthX = legacy;
		m_refractionStrengthY = legacy;
	}
	if (j.contains("refractionStrengthX")) {
		m_refractionStrengthX = j.at("refractionStrengthX").get<float>();
	}
	if (j.contains("refractionStrengthY")) {
		m_refractionStrengthY = j.at("refractionStrengthY").get<float>();
	}
	if (j.contains("baseOffsetX")) {
		m_baseOffsetX = j.at("baseOffsetX").get<float>();
	}
	if (j.contains("baseOffsetY")) {
		m_baseOffsetY = j.at("baseOffsetY").get<float>();
	}
	if (j.contains("normalStrength")) {
		m_normalStrength = j.at("normalStrength").get<float>();
	}
	if (j.contains("normalTiling")) {
		m_normalTiling = j.at("normalTiling").get<float>();
	}
	if (j.contains("opacity")) {
		m_opacity = j.at("opacity").get<float>();
	}
	if (j.contains("tintMix")) {
		m_tintMix = j.at("tintMix").get<float>();
	}
	if (j.contains("flipSceneY")) {
		m_flipSceneY = j.at("flipSceneY").get<bool>();
	}
	if (j.contains("waveSpeed1")) {
		m_waveSpeed1 = j.at("waveSpeed1").get<float>();
	}
	if (j.contains("waveSpeed2")) {
		m_waveSpeed2 = j.at("waveSpeed2").get<float>();
	}
	if (j.contains("distortionMix")) {
		m_distortionMix = j.at("distortionMix").get<float>();
	}
	if (j.contains("straightDistortStrength")) {
		m_straightDistortStrength = j.at("straightDistortStrength").get<float>();
	}
	if (j.contains("straightBlend")) {
		m_straightBlend = j.at("straightBlend").get<float>();
	}
	if (j.contains("verticalPull")) {
		m_verticalPull = j.at("verticalPull").get<float>();
	}
	if (j.contains("causticStrength")) {
		m_causticStrength = j.at("causticStrength").get<float>();
	}
	if (j.contains("causticScale")) {
		m_causticScale = j.at("causticScale").get<float>();
	}
	if (j.contains("causticSpeed")) {
		m_causticSpeed = j.at("causticSpeed").get<float>();
	}
	if (j.contains("causticSharpness")) {
		m_causticSharpness = j.at("causticSharpness").get<float>();
	}
	if (j.contains("tint")) {
		auto tint = j.at("tint");
		m_tint = glm::vec4(tint[0].get<float>(), tint[1].get<float>(), tint[2].get<float>(), tint[3].get<float>());
	}
}

json_t WaterRendererComponent::Save() const {
	json_t j = MeshRendererComponent::Save();
	j["refractionStrengthX"] = m_refractionStrengthX;
	j["refractionStrengthY"] = m_refractionStrengthY;
	j["baseOffsetX"] = m_baseOffsetX;
	j["baseOffsetY"] = m_baseOffsetY;
	j["normalStrength"] = m_normalStrength;
	j["normalTiling"] = m_normalTiling;
	j["opacity"] = m_opacity;
	j["tintMix"] = m_tintMix;
	j["flipSceneY"] = m_flipSceneY;
	j["waveSpeed1"] = m_waveSpeed1;
	j["waveSpeed2"] = m_waveSpeed2;
	j["distortionMix"] = m_distortionMix;
	j["straightDistortStrength"] = m_straightDistortStrength;
	j["straightBlend"] = m_straightBlend;
	j["verticalPull"] = m_verticalPull;
	j["causticStrength"] = m_causticStrength;
	j["causticScale"] = m_causticScale;
	j["causticSpeed"] = m_causticSpeed;
	j["causticSharpness"] = m_causticSharpness;
	j["tint"] = { m_tint.r, m_tint.g, m_tint.b, m_tint.a };
	return j;
}

#ifdef TOAST_EDITOR
void WaterRendererComponent::Inspector() {
	MeshRendererComponent::Inspector();

	auto apply_default = [this]() {
		m_refractionStrengthX = 0.02f;
		m_refractionStrengthY = 0.02f;
		m_baseOffsetX = 0.0f;
		m_baseOffsetY = 0.0f;
		m_normalStrength = 1.0f;
		m_normalTiling = 1.0f;
		m_opacity = 0.85f;
		m_tint = glm::vec4(0.7f, 0.9f, 1.0f, 0.75f);
		m_tintMix = 0.6f;
		m_flipSceneY = true;
		m_waveSpeed1 = 0.08f;
		m_waveSpeed2 = 0.05f;
		m_distortionMix = 0.5f;
		m_straightDistortStrength = 0.01f;
		m_straightBlend = 0.35f;
		m_verticalPull = 0.02f;
		m_causticStrength = 0.12f;
		m_causticScale = 18.0f;
		m_causticSpeed = 0.8f;
		m_causticSharpness = 2.0f;
	};

	auto apply_subtle = [this, &apply_default]() {
		apply_default();
		m_refractionStrengthX = 0.01f;
		m_refractionStrengthY = 0.01f;
		m_waveSpeed1 = 0.04f;
		m_waveSpeed2 = 0.03f;
		m_straightBlend = 0.2f;
		m_causticStrength = 0.06f;
		m_opacity = 0.78f;
	};

	auto apply_stylized = [this, &apply_default]() {
		apply_default();
		m_refractionStrengthX = 0.06f;
		m_refractionStrengthY = 0.045f;
		m_normalStrength = 1.8f;
		m_normalTiling = 2.5f;
		m_waveSpeed1 = 0.22f;
		m_waveSpeed2 = 0.16f;
		m_straightDistortStrength = 0.03f;
		m_straightBlend = 0.55f;
		m_verticalPull = 0.05f;
		m_causticStrength = 0.35f;
		m_causticScale = 26.0f;
		m_causticSpeed = 1.6f;
		m_causticSharpness = 2.8f;
	};

	ImGui::SeparatorText("Water Refraction");
	if (ImGui::Button("Reset Defaults")) {
		apply_default();
	}
	ImGui::SameLine();
	if (ImGui::Button("Preset: Subtle")) {
		apply_subtle();
	}
	ImGui::SameLine();
	if (ImGui::Button("Preset: Stylized")) {
		apply_stylized();
	}

	ImGui::SeparatorText("Surface");
	ImGui::ColorEdit4("Water Tint", &m_tint.r);
	ImGui::SliderFloat("Tint Mix", &m_tintMix, 0.0f, 1.0f, "%.3f");
	ImGui::SliderFloat("Opacity", &m_opacity, 0.0f, 1.0f, "%.3f");
	ImGui::Checkbox("Flip Scene Y", &m_flipSceneY);

	ImGui::SeparatorText("Refraction");
	ImGui::SliderFloat("Refraction X", &m_refractionStrengthX, -0.3f, 0.3f, "%.4f");
	ImGui::SliderFloat("Refraction Y", &m_refractionStrengthY, -0.3f, 0.3f, "%.4f");
	ImGui::SliderFloat("Base Offset X", &m_baseOffsetX, -0.2f, 0.2f, "%.4f");
	ImGui::SliderFloat("Base Offset Y", &m_baseOffsetY, -0.2f, 0.2f, "%.4f");
	ImGui::SliderFloat("Straight Distort", &m_straightDistortStrength, 0.0f, 0.2f, "%.4f");
	ImGui::SliderFloat("Straight Blend", &m_straightBlend, 0.0f, 1.0f, "%.3f");
	ImGui::SliderFloat("Vertical Pull", &m_verticalPull, 0.0f, 0.3f, "%.4f");

	ImGui::SeparatorText("Normals & Waves");
	ImGui::SliderFloat("Normal Strength", &m_normalStrength, 0.0f, 5.0f, "%.3f");
	ImGui::SliderFloat("Normal Tiling", &m_normalTiling, 0.01f, 32.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Wave Speed 1", &m_waveSpeed1, -2.0f, 2.0f, "%.3f");
	ImGui::SliderFloat("Wave Speed 2", &m_waveSpeed2, -2.0f, 2.0f, "%.3f");
	ImGui::SliderFloat("Distortion Mix", &m_distortionMix, 0.0f, 1.0f, "%.3f");

	ImGui::SeparatorText("Caustics");
	ImGui::SliderFloat("Caustic Strength", &m_causticStrength, 0.0f, 1.5f, "%.3f");
	ImGui::SliderFloat("Caustic Scale", &m_causticScale, 1.0f, 64.0f, "%.2f");
	ImGui::SliderFloat("Caustic Speed", &m_causticSpeed, -4.0f, 4.0f, "%.3f");
	ImGui::SliderFloat("Caustic Sharpness", &m_causticSharpness, 0.1f, 8.0f, "%.2f");

}
#endif

void WaterRendererComponent::SetRefractionTexture(unsigned int texture_id, int texture_unit) {
	m_refractionTextureId = texture_id;
	m_refractionTextureUnit = texture_unit;
}

void WaterRendererComponent::SetTransparent(bool /*transparent*/) {
	MeshRendererComponent::SetTransparent(true);
}

void WaterRendererComponent::RegisterWithRenderer(renderer::IRendererBase* renderer) {
	renderer->AddWater(this);
}

void WaterRendererComponent::UnregisterFromRenderer(renderer::IRendererBase* renderer) {
	renderer->RemoveWater(this);
}

void WaterRendererComponent::EnableInRenderer(renderer::IRendererBase* renderer) {
	renderer->EnableWater(this);
}

void WaterRendererComponent::DisableInRenderer(renderer::IRendererBase* renderer) {
	renderer->DisableWater(this);
}

void WaterRendererComponent::ApplyCustomUniforms(renderer::Shader* shader) noexcept {
	if (!shader) {
		return;
	}

	shader->Set("gColor", m_tint);
	shader->Set("uRefractionStrengthX", m_refractionStrengthX);
	shader->Set("uRefractionStrengthY", m_refractionStrengthY);
	shader->Set("uBaseOffsetX", m_baseOffsetX);
	shader->Set("uBaseOffsetY", m_baseOffsetY);
	shader->Set("uNormalStrength", m_normalStrength);
	shader->Set("uNormalTiling", m_normalTiling);
	shader->Set("uOpacity", m_opacity);
	shader->Set("uTintMix", m_tintMix);
	shader->Set("uFlipY", m_flipSceneY ? 1.0f : 0.0f);
	shader->Set("uWaveSpeed1", m_waveSpeed1);
	shader->Set("uWaveSpeed2", m_waveSpeed2);
	shader->Set("uDistortionMix", m_distortionMix);
	shader->Set("uStraightDistortStrength", m_straightDistortStrength);
	shader->Set("uStraightBlend", m_straightBlend);
	shader->Set("uVerticalPull", m_verticalPull);
	shader->Set("uCausticStrength", m_causticStrength);
	shader->Set("uCausticScale", m_causticScale);
	shader->Set("uCausticSpeed", m_causticSpeed);
	shader->Set("uCausticSharpness", m_causticSharpness);
}

}








