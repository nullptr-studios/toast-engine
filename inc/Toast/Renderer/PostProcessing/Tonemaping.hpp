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

	[[nodiscard]]
	std::string_view GetTypeId() const override {
		return "Tonemaping";
	}

	void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) override;
	[[nodiscard]]
	json_t SaveParams() const override;
	void LoadParams(const json_t& j) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	float m_gamma = 1.f;
	float m_exposure = 1.f;

	std::shared_ptr<renderer::Shader> m_tonemapShader;
};
