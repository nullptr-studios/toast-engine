/// @file WaterRendererComponent.hpp
/// @brief Dedicated world-space water renderer with scene-refraction support.

#pragma once

#include "Toast/Components/MeshRendererComponent.hpp"

namespace toast {

class WaterRendererComponent : public MeshRendererComponent {
public:
	REGISTER_TYPE(WaterRendererComponent);

	void Init() override;
	void LoadTextures() override;
	void OnRender(renderer::IRenderablePass pass, const glm::mat4& precomputed_mat) noexcept override;

	void Load(json_t j, bool force_create = true) override;
	[[nodiscard]]
	json_t Save() const override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	void SetRefractionTexture(unsigned int texture_id, int texture_unit = 7);

	void SetTransparent(bool transparent) override;

protected:
	void RegisterWithRenderer(renderer::IRendererBase* renderer) override;
	void UnregisterFromRenderer(renderer::IRendererBase* renderer) override;
	void EnableInRenderer(renderer::IRendererBase* renderer) override;
	void DisableInRenderer(renderer::IRendererBase* renderer) override;
	void ApplyCustomUniforms(renderer::Shader* shader) noexcept override;

private:
	float m_refractionStrengthX = 0.02f;
	float m_refractionStrengthY = 0.02f;
	float m_baseOffsetX = 0.0f;
	float m_baseOffsetY = 0.0f;
	float m_normalStrength = 1.0f;
	float m_normalTiling = 1.0f;
	float m_opacity = 0.85f;
	glm::vec4 m_tint = glm::vec4(0.7f, 0.9f, 1.0f, 0.75f);
	float m_tintMix = 0.6f;
	bool m_flipSceneY = true;
	float m_waveSpeed1 = 0.08f;
	float m_waveSpeed2 = 0.05f;
	float m_distortionMix = 0.5f;
	float m_straightDistortStrength = 0.01f;
	float m_straightBlend = 0.35f;
	float m_verticalPull = 0.02f;
	float m_causticStrength = 0.12f;
	float m_causticScale = 18.0f;
	float m_causticSpeed = 0.8f;
	float m_causticSharpness = 2.0f;

	unsigned int m_refractionTextureId = 0;
	int m_refractionTextureUnit = 7;

	std::shared_ptr<renderer::Shader> m_waterShader;
	std::shared_ptr<Texture> m_normalTexture;
};

}




