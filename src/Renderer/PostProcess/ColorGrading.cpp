/// @file ColorGrading.cpp
/// @author dario
/// @date 25/03/2026.

#include "Toast/Renderer/PostProcessing/ColorGrading.hpp"

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

Colorgrading::Colorgrading() {
	m_colorgradingShader = resource::LoadResource<renderer::Shader>("SHADERS/colorgrading.shader");
}

void Colorgrading::Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) {
	if (!m_colorgradingShader || !inputFBO || !outputFBO) {
		return;
	}

	GLuint inTex = inputFBO->GetColorTexture(0);

	outputFBO->bindDraw();

	glViewport(0, 0, outputFBO->Width(), outputFBO->Height());
	glScissor(0, 0, outputFBO->Width(), outputFBO->Height());

	// OMG this took me hours
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);

	glBindTextureUnit(0, inTex);
	m_colorgradingShader->Use();
	m_colorgradingShader->SetSampler("uInputTex", 0);

	m_colorgradingShader->Set("uContrast", contrast);
	m_colorgradingShader->Set("uSaturation", saturation);
	m_colorgradingShader->Set("uTint", tint);

	m_colorgradingShader->Set("uLift", lift);
	m_colorgradingShader->Set("uGamma", gamma);
	m_colorgradingShader->Set("uGain", gain);

	renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

	glBindTextureUnit(0, 0);

	Framebuffer::unbind();
}

#ifdef TOAST_EDITOR
void Colorgrading::Inspector() {
	ImGui::SeparatorText("Color Grading");
	ImGui::DragFloat("Contrast", &contrast, .01f, 0.f, 10.f);
	ImGui::DragFloat("Saturation", &saturation, .01f, 0.f, 10.f);
	ImGui::ColorPicker3("Tint", (float*)&tint, ImGuiColorEditFlags_PickerHueWheel);
	ImGui::Spacing();
	ImGui::ColorPicker3("Lift", (float*)&lift, ImGuiColorEditFlags_PickerHueWheel);
	ImGui::ColorPicker3("Gamma", (float*)&gamma, ImGuiColorEditFlags_PickerHueWheel);
	ImGui::ColorPicker3("Gain", (float*)&gain, ImGuiColorEditFlags_PickerHueWheel);
}
#endif
