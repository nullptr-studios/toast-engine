/// @file Tonemaping.cpp
/// @author dario
/// @date 24/03/2026.

#include "Toast/Renderer/PostProcessing/Tonemaping.hpp"

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OpenGL/GLStateCache.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/Texture.hpp"

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
	renderer::SetBlend(false);
	renderer::SetDepthTest(false);
	renderer::SetCullFace(false);
	renderer::SetDepthMask(false);

	Texture::BindTextureId(0, inTex);

	m_tonemapShader->Use();
	m_tonemapShader->SetSampler("uInputTex", 0);
	m_tonemapShader->Set("uExposure", m_exposure);
	m_tonemapShader->Set("uGamma", m_gamma);

	renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

	Texture::UnbindTextureUnit(0);

	Framebuffer::unbind();
}

json_t Tonemaping::SaveParams() const {
	json_t j {};
	j["exposure"] = m_exposure;
	j["gamma"] = m_gamma;
	return j;
}

void Tonemaping::LoadParams(const json_t& j) {
	m_exposure = j.value("exposure", m_exposure);
	m_gamma = j.value("gamma", m_gamma);
}

#ifdef TOAST_EDITOR

void Tonemaping::Inspector() {
	ImGui::SeparatorText("ACES Tonemapping");
	ImGui::DragFloat("Exposure", &m_exposure, .01f);
	ImGui::DragFloat("Gamma", &m_gamma, .01f);
}

#endif
