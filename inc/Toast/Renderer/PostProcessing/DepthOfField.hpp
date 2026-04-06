/// @file DepthOfField.hpp
/// @author dario
/// @date 06/04/2026.

#pragma once

#include "Toast/Renderer/IPostProcess.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <memory>

struct DepthOfField : public IPostProcess {
	DepthOfField() {
		m_shader = resource::LoadResource<renderer::Shader>("SHADERS/depth_of_field.shader");
	}

	[[nodiscard]]
	std::string_view GetTypeId() const override {
		return "DepthOfField";
	}

	void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) override {
		if (!m_shader || !inputFBO || !outputFBO) {
			return;
		}

		auto* renderer = renderer::IRendererBase::GetInstance();
		auto* geometry = renderer ? renderer->GetGeometryFramebuffer() : nullptr;
		GLuint depthTex = geometry ? geometry->GetDepthTexture() : 0;

		outputFBO->bindDraw();
		glViewport(0, 0, outputFBO->Width(), outputFBO->Height());
		glScissor(0, 0, outputFBO->Width(), outputFBO->Height());

		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDepthMask(GL_FALSE);

		glBindTextureUnit(0, inputFBO->GetColorTexture(0));
		glBindTextureUnit(1, depthTex);

		m_shader->Use();
		m_shader->SetSampler("uInputTex", 0);
		m_shader->SetSampler("uDepthTex", 1);
		m_shader->Set("uFocusDistance", m_focusDistance);
		m_shader->Set("uFocusRange", m_focusRange);
		m_shader->Set("uBlurStrength", m_blurStrength * GetBlend());
		m_shader->Set("uNearBlurScale", m_nearBlurScale);
		m_shader->Set("uFarBlurScale", m_farBlurScale);
		m_shader->Set("uNearPlane", m_nearPlane);
		m_shader->Set("uFarPlane", m_farPlane);
		m_shader->Set("uUseDepth", depthTex ? 1 : 0);

		renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

		glBindTextureUnit(1, 0);
		glBindTextureUnit(0, 0);
		Framebuffer::unbind();
	}

	[[nodiscard]]
	json_t SaveParams() const override {
		json_t j {};
		j["focusDistance"] = m_focusDistance;
		j["focusRange"] = m_focusRange;
		j["blurStrength"] = m_blurStrength;
		j["nearBlurScale"] = m_nearBlurScale;
		j["farBlurScale"] = m_farBlurScale;
		j["nearPlane"] = m_nearPlane;
		j["farPlane"] = m_farPlane;
		return j;
	}

	void LoadParams(const json_t& j) override {
		m_focusDistance = j.value("focusDistance", m_focusDistance);
		m_focusRange = j.value("focusRange", m_focusRange);
		m_blurStrength = j.value("blurStrength", m_blurStrength);
		m_nearBlurScale = j.value("nearBlurScale", m_nearBlurScale);
		m_farBlurScale = j.value("farBlurScale", m_farBlurScale);
		m_nearPlane = j.value("nearPlane", m_nearPlane);
		m_farPlane = j.value("farPlane", m_farPlane);
	}

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	float m_focusDistance = 7.0f;
	float m_focusRange = 2.0f;
	float m_blurStrength = 2.0f;
	float m_nearBlurScale = 1.25f;
	float m_farBlurScale = 1.0f;
	float m_nearPlane = 0.1f;
	float m_farPlane = 1000.0f;
	std::shared_ptr<renderer::Shader> m_shader;
};

#ifdef TOAST_EDITOR
#include "imgui.h"
inline void DepthOfField::Inspector() {
	ImGui::SeparatorText("Depth Of Field");
	ImGui::DragFloat("Near Plane", &m_nearPlane, 0.01f, 0.01f, 10.0f);
	ImGui::DragFloat("Far Plane", &m_farPlane, 1.0f, 1.0f, 5000.0f);
	if (m_farPlane <= m_nearPlane + 0.01f) {
		m_farPlane = m_nearPlane + 0.01f;
	}
	ImGui::DragFloat("Focus Distance", &m_focusDistance, 0.1f, m_nearPlane, m_farPlane);
	ImGui::DragFloat("Focus Range", &m_focusRange, 0.05f, 0.01f, 200.0f);
	ImGui::DragFloat("Blur Strength", &m_blurStrength, 0.01f, 0.0f, 16.0f);
	ImGui::DragFloat("Near Blur Scale", &m_nearBlurScale, 0.01f, 0.0f, 4.0f);
	ImGui::DragFloat("Far Blur Scale", &m_farBlurScale, 0.01f, 0.0f, 4.0f);
}
#endif


