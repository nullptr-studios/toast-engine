/// @file ColorGrading.hpp
/// @author dario
/// @date 25/03/2026.

#pragma once
#include "Toast/Renderer/IPostProcess.hpp"
#include "Toast/Renderer/Shader.hpp"

struct Colorgrading : public IPostProcess {
	Colorgrading();

	[[nodiscard]]
	std::string_view GetTypeId() const override {
		return "ColorGrading";
	}

	void Execute(Framebuffer* inputFBO, Framebuffer* outputFBO) override;
	[[nodiscard]]
	json_t SaveParams() const override;
	void LoadParams(const json_t& j) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

private:
	float contrast = 1.15f;
	float saturation = 0.95f;
	glm::vec3 tint = glm::vec3(1.0f);

	glm::vec3 lift = glm::vec3(0.02, 0.015, 0.01);
	glm::vec3 gamma = glm::vec3(0.95);
	glm::vec3 gain = glm::vec3(1.1, 1.05, 1.0);

	std::shared_ptr<renderer::Shader> m_colorgradingShader;
};
