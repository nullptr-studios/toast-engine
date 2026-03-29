/// @file Tonemaping.hpp
/// @author dario
/// @date 24/03/2026.

#pragma once
#include "Toast/Renderer/IPostProcess.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <memory>

struct Tonemaping : public IPostProcess {
	Tonemaping();

	void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	float m_gamma = 1.f;
	float m_exposure = 1.f;

	std::shared_ptr<renderer::Shader> m_tonemapShader;
};
