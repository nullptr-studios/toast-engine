/// @file HUDWorldRendererComponent.hpp
/// @brief World-space HUD renderer that draws an Ultralight texture directly on a mesh.

#pragma once

#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#include <Ultralight/Ultralight.h>

#include <string>

namespace renderer::HUD {
class HUDLayer;
}

namespace toast {

class HUDWorldRendererComponent : public IRenderable {
public:
	REGISTER_TYPE(HUDWorldRendererComponent);

	void Init() override;
	void LoadTextures() override;
	void OnRender(renderer::IRenderablePass pass, const glm::mat4& precomputed_mat) noexcept override;
	void Destroy() override;
	void OnEnable() override;
	void OnDisable() override;

	void Load(json_t j, bool force_create = true) override;
	[[nodiscard]]
	json_t Save() const override;

	void SetTexture(unsigned int texture_id) {
		m_textureId = texture_id;
	}

	void SetView(ultralight::RefPtr<ultralight::View> view, renderer::HUD::HUDLayer* hud_layer) {
		m_view = std::move(view);
		m_hudLayer = hud_layer;
	}

	void ClearView() {
		m_view = nullptr;
		m_hudLayer = nullptr;
		m_textureId = 0;
	}

	void SetMeshPath(const std::string& path) {
		m_meshPath = path;
		m_mesh = resource::LoadResource<renderer::Mesh>(m_meshPath);
	}

	void SetDrawToDepth(bool value) {
		m_drawToDepth = value;
	}

private:
	std::string m_meshPath = "MODELS/quad.obj";
	std::shared_ptr<renderer::Mesh> m_mesh;
	std::shared_ptr<renderer::Shader> m_shader;
	ultralight::RefPtr<ultralight::View> m_view;
	renderer::HUD::HUDLayer* m_hudLayer = nullptr;

	unsigned int m_textureId = 0;
	bool m_drawToDepth = false;
	bool m_registered = false;
};

}


