/// @file ChromaticAberration.hpp
/// @author dario
/// @date 06/04/2026.

#pragma once

#include "Toast/Renderer/IPostProcess.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <memory>

struct ChromaticAberration : public IPostProcess {
	ChromaticAberration();

	[[nodiscard]]
	std::string_view GetTypeId() const override {
		return "ChromaticAberration";
	}

	void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) override;
	[[nodiscard]]
	json_t SaveParams() const override;
	void LoadParams(const json_t& j) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	float m_strength = 0.0035f;
	float m_falloff = 1.8f;
	std::shared_ptr<renderer::Shader> m_shader;
};

inline ChromaticAberration::ChromaticAberration() {
	m_shader = resource::LoadResource<renderer::Shader>("SHADERS/chromatic_aberration.shader");
}

inline void ChromaticAberration::Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) {
	if (!m_shader || !inputFBO || !outputFBO) {
		return;
	}

	outputFBO->bindDraw();
	glViewport(0, 0, outputFBO->Width(), outputFBO->Height());
	glScissor(0, 0, outputFBO->Width(), outputFBO->Height());

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);

	glBindTextureUnit(0, inputFBO->GetColorTexture(0));
	m_shader->Use();
	m_shader->SetSampler("uInputTex", 0);
	m_shader->Set("uStrength", m_strength * GetBlend());
	m_shader->Set("uFalloff", m_falloff);

	renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

	glBindTextureUnit(0, 0);
	Framebuffer::unbind();
}

inline json_t ChromaticAberration::SaveParams() const {
	json_t j {};
	j["strength"] = m_strength;
	j["falloff"] = m_falloff;
	return j;
}

inline void ChromaticAberration::LoadParams(const json_t& j) {
	m_strength = j.value("strength", m_strength);
	m_falloff = j.value("falloff", m_falloff);
}

#ifdef TOAST_EDITOR
#include "imgui.h"
inline void ChromaticAberration::Inspector() {
	ImGui::SeparatorText("Chromatic Aberration");
	ImGui::DragFloat("Strength", &m_strength, 0.0001f, 0.0f, 0.05f, "%.4f");
	ImGui::DragFloat("Falloff", &m_falloff, 0.01f, 0.1f, 8.0f);
}
#endif


