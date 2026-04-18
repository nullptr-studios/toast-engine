/// @file Bloom.cpp
/// @author dario
/// @date 24/03/2026.

#include "Toast/Renderer/PostProcessing/Bloom.hpp"

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/OpenGL/GLStateCache.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/Texture.hpp"

#include <algorithm>

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

Bloom::Bloom() {
	m_extractShader = resource::LoadResource<renderer::Shader>("SHADERS/bloom_extract.shader");
	m_blurShader = resource::LoadResource<renderer::Shader>("SHADERS/bloom_blur.shader");
	m_compositeShader = resource::LoadResource<renderer::Shader>("SHADERS/bloom_composite.shader");
}

Bloom::~Bloom() {
	delete m_extractFbo;
	delete m_blurFboA;
	delete m_blurFboB;
}

void Bloom::EnsureBuffers(int width, int height) {
	const int scaledWidth = std::max(1, static_cast<int>(width * m_resolutionScale));
	const int scaledHeight = std::max(1, static_cast<int>(height * m_resolutionScale));
	Framebuffer::Specs specs = { scaledWidth, scaledHeight };

	if (!m_extractFbo) {
		m_extractFbo = new Framebuffer(specs);
		m_extractFbo->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_extractFbo->Build();
	} else if (m_extractFbo->Width() != scaledWidth || m_extractFbo->Height() != scaledHeight) {
		m_extractFbo->Resize(scaledWidth, scaledHeight);
	}

	if (!m_blurFboA) {
		m_blurFboA = new Framebuffer(specs);
		m_blurFboA->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_blurFboA->Build();
	} else if (m_blurFboA->Width() != scaledWidth || m_blurFboA->Height() != scaledHeight) {
		m_blurFboA->Resize(scaledWidth, scaledHeight);
	}

	if (!m_blurFboB) {
		m_blurFboB = new Framebuffer(specs);
		m_blurFboB->AddColorAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		m_blurFboB->Build();
	} else if (m_blurFboB->Width() != scaledWidth || m_blurFboB->Height() != scaledHeight) {
		m_blurFboB->Resize(scaledWidth, scaledHeight);
	}
}

void Bloom::Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) {
	if (!m_extractShader || !m_blurShader || !m_compositeShader || !inputFBO || !outputFBO) {
		return;
	}
	EnsureBuffers(outputFBO->Width(), outputFBO->Height());

	if (!m_extractFbo || !m_blurFboA || !m_blurFboB) {
		return;
	}

	const float blendIntensity = m_intensity * GetBlend();
	if (blendIntensity <= 0.0f) {
		inputFBO->BlitTo(outputFBO, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		return;
	}

	// 1) Extract HDR highlights above threshold (values > 1 are kept and boosted naturally).
	m_extractFbo->bindDraw();
	glViewport(0, 0, m_extractFbo->Width(), m_extractFbo->Height());
	glScissor(0, 0, m_extractFbo->Width(), m_extractFbo->Height());
	renderer::SetBlend(false);
	renderer::SetDepthTest(false);
	renderer::SetCullFace(false);
	renderer::SetDepthMask(false);
	Texture::BindTextureId(0, inputFBO->GetColorTexture(0));
	m_extractShader->Use();
	m_extractShader->SetSampler("uInputTex", 0);
	m_extractShader->Set("uThreshold", m_threshold);
	m_extractShader->Set("uKnee", m_knee);
	renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

	// 2) Separable Gaussian blur using convolution kernel.
	Framebuffer* srcBlurFbo = m_extractFbo;
	Framebuffer* dstBlurFbo = m_blurFboA;
	const int blurPasses = std::max(1, m_iterations * 2);
	for (int pass = 0; pass < blurPasses; ++pass) {
		const bool horizontal = (pass % 2) == 0;
		dstBlurFbo->bindDraw();
		glViewport(0, 0, dstBlurFbo->Width(), dstBlurFbo->Height());
		glScissor(0, 0, dstBlurFbo->Width(), dstBlurFbo->Height());
		Texture::BindTextureId(0, srcBlurFbo->GetColorTexture(0));
		m_blurShader->Use();
		m_blurShader->SetSampler("uInputTex", 0);
		m_blurShader->Set("uHorizontal", horizontal ? 1 : 0);
		m_blurShader->Set("uSigma", m_blurSigma);
		renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

		srcBlurFbo = dstBlurFbo;
		dstBlurFbo = (dstBlurFbo == m_blurFboA) ? m_blurFboB : m_blurFboA;
	}

	// 3) Composite original + blurred bloom into output.
	outputFBO->bindDraw();
	glViewport(0, 0, outputFBO->Width(), outputFBO->Height());
	glScissor(0, 0, outputFBO->Width(), outputFBO->Height());

	renderer::SetBlend(false);
	renderer::SetDepthTest(false);
	renderer::SetCullFace(false);
	renderer::SetDepthMask(false);

	Texture::BindTextureId(0, inputFBO->GetColorTexture(0));
	Texture::BindTextureId(1, srcBlurFbo->GetColorTexture(0));
	m_compositeShader->Use();
	m_compositeShader->SetSampler("uInputTex", 0);
	m_compositeShader->SetSampler("uBloomTex", 1);
	m_compositeShader->Set("uIntensity", blendIntensity);

	renderer::IRendererBase::GetInstance()->DrawScreenQuad(false, false);

	Texture::UnbindTextureUnit(1);
	Texture::UnbindTextureUnit(0);
	Framebuffer::unbind();
}

json_t Bloom::SaveParams() const {
	json_t j {};
	j["threshold"] = m_threshold;
	j["knee"] = m_knee;
	j["intensity"] = m_intensity;
	j["blurSigma"] = m_blurSigma;
	j["resolutionScale"] = m_resolutionScale;
	j["iterations"] = m_iterations;
	return j;
}

void Bloom::LoadParams(const json_t& j) {
	m_threshold = j.value("threshold", m_threshold);
	m_knee = j.value("knee", m_knee);
	m_intensity = j.value("intensity", m_intensity);
	m_blurSigma = j.value("blurSigma", m_blurSigma);
	m_resolutionScale = std::clamp(j.value("resolutionScale", m_resolutionScale), 0.1f, 1.0f);
	m_iterations = std::clamp(j.value("iterations", m_iterations), 1, 8);
}

#ifdef TOAST_EDITOR
void Bloom::Inspector() {
	ImGui::SeparatorText("Bloom");
	ImGui::DragFloat("Threshold", &m_threshold, 0.01f, 0.0f, 10.0f);
	ImGui::SliderFloat("Knee", &m_knee, 0.0f, 1.0f);
	ImGui::DragFloat("Intensity", &m_intensity, 0.01f, 0.0f, 5.0f);
	ImGui::SliderFloat("Blur Sigma", &m_blurSigma, 0.5f, 5.0f);
	ImGui::SliderFloat("Resolution Scale", &m_resolutionScale, 0.1f, 1.0f);
	ImGui::SliderInt("Iterations", &m_iterations, 1, 8);
}
#endif

