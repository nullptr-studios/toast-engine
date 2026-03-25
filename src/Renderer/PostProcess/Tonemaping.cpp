/// @file Tonemaping.cpp
/// @author dario
/// @date 24/03/2026.

#include "Toast/Renderer/PostProcessing/Tonemaping.hpp"

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

Tonemaping::Tonemaping() {
	m_tonemapShader = resource::LoadResource<renderer::Shader>("SHADERS/tonemapping.shader");
}

void Tonemaping::Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) {
	if (!m_tonemapShader || !inputFBO || !outputFBO) {
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

	m_tonemapShader->Use();
	m_tonemapShader->SetSampler("uInputTex", 0);
	m_tonemapShader->Set("uExposure", m_exposure);
	m_tonemapShader->Set("uGamma", m_gamma);

	renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

	glBindTextureUnit(0, 0);

	Framebuffer::unbind();
}

#ifdef TOAST_EDITOR

void Tonemaping::Inspector() {
	ImGui::SeparatorText("ACES Tonemapping");
	ImGui::DragFloat("Exposure", &m_exposure, .01f);
	ImGui::DragFloat("Gamma", &m_gamma, .01f);
}

#endif
