/// @file Bloom.hpp
/// @author dario
/// @date 24/03/2026.

#pragma once

#include "Toast/Renderer/Framebuffer.hpp"
#include "Toast/Renderer/IPostProcess.hpp"
#include "Toast/Renderer/Shader.hpp"

#include <memory>

struct Bloom : public IPostProcess {
	Bloom();
	~Bloom() override;

	[[nodiscard]]
	std::string_view GetTypeId() const override {
		return "Bloom";
	}

	void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) override;
	[[nodiscard]]
	json_t SaveParams() const override;
	void LoadParams(const json_t& j) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	void EnsureBuffers(int width, int height);

	float m_threshold = 1.0f;
	float m_knee = 0.5f;
	float m_intensity = 0.45f;
	float m_blurSigma = 2.2f;
	float m_resolutionScale = 0.5f;
	int m_iterations = 2;

	Framebuffer* m_extractFbo = nullptr;
	Framebuffer* m_blurFboA = nullptr;
	Framebuffer* m_blurFboB = nullptr;

	std::shared_ptr<renderer::Shader> m_extractShader;
	std::shared_ptr<renderer::Shader> m_blurShader;
	std::shared_ptr<renderer::Shader> m_compositeShader;
};
