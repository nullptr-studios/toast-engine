/// @file SpriteRenderComponent.hpp
/// @author dario
/// @date 28/09/2025.

#pragma once

#include "Toast/Components/TransformComponent.hpp"
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Material.hpp"
#include "Toast/Renderer/Shader.hpp"
#include "Toast/Resources/Mesh.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Resources/ResourceSlot.hpp"
#include "Toast/Resources/Texture.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <utility>

namespace renderer {
class IRendererBase;
}

namespace toast {
class MeshRendererComponent : public IRenderable {
public:
	REGISTER_TYPE(MeshRendererComponent);

	void Load(json_t j, bool force_create = true) override;
	[[nodiscard]]
	json_t Save() const override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

	void Init() override;
	void LoadTextures() override;

	void OnRender(renderer::IRenderablePass pass, const glm::mat4& precomputed_mat) noexcept override;

	void Destroy() override;

	void OnEnable() override;
	void OnDisable() override;

	void SetMaterial(const std::string& path) {
		// m_materialSlot.SetResource(path);
		m_materialPath = path;
		m_material = resource::LoadResource<renderer::Material>(path);
	}

	//[[nodiscard]]
	// std::weak_ptr<Texture> GetTexture() const {
	//	return m_texture;
	//}
	//
	// void SetTexture(const std::shared_ptr<Texture>& texture) {
	//	m_texture = texture;
	//}
	//
	// void SetTexture(const std::string_view& path) {
	//	m_texturePath = path;
	//	m_texture = resource::ResourceManager::GetInstance()->LoadResource<Texture>(m_texturePath);
	//}
	//
	//[[nodiscard]]
	// std::weak_ptr<renderer::Shader> GetShader() const {
	//	return m_shader;
	//}
	//
	// void SetShader(const std::shared_ptr<renderer::Shader>& shader) {
	//	m_shader = shader;
	//}
	//
	// void SetShader(const std::string_view& path) {
	//	m_shaderPath = path;
	//	m_shader = resource::ResourceManager::GetInstance()->LoadResource<renderer::Shader>(m_shaderPath);
	//}

	[[nodiscard]]
	std::weak_ptr<renderer::Mesh> GetMesh() const {
		return m_mesh;
	}

	void SetMesh(const std::shared_ptr<renderer::Mesh>& mesh) {
		m_mesh = mesh;
	}

	void SetMesh(const std::string_view& path) {
		m_meshPath = path;
		m_mesh = resource::LoadResource<renderer::Mesh>(m_meshPath);
	}

	virtual void SetTransparent(bool transparent);

	void SetUseExternalTextureOnly(bool enabled) {
		m_useExternalTextureOnly = enabled;
	}

	[[nodiscard]]
	bool IsTransparent() const {
		return m_isTransparent;
	}

	[[nodiscard]]
	bool WritesDepthInGeometryPass() const noexcept override {
		return m_drawToDepth;
	}

	void SetExternalTexture(unsigned int texture_id, std::string sampler_name = "gTexture", int texture_unit = 0) {
		m_externalTextureId = texture_id;
		m_externalTextureSampler = std::move(sampler_name);
		m_externalTextureUnit = texture_unit;
		m_useExternalTexture = (texture_id != 0);
	}

	void ClearExternalTexture() {
		m_externalTextureId = 0;
		m_useExternalTexture = false;
	}

	// Vertex color controls the generic vertex attribute (location = 3) used by the shaders
	//[[nodiscard]]
	// glm::vec4 GetVertexColor() const { return m_vertexColor; }

	// void SetVertexColor(const glm::vec4& c) { m_vertexColor = c; }

protected:
	virtual void RegisterWithRenderer(renderer::IRendererBase* renderer);
	virtual void UnregisterFromRenderer(renderer::IRendererBase* renderer);
	virtual void EnableInRenderer(renderer::IRendererBase* renderer);
	virtual void DisableInRenderer(renderer::IRendererBase* renderer);
	virtual void ApplyCustomUniforms(renderer::Shader* shader) noexcept;

	[[nodiscard]]
	const std::string& GetMaterialPath() const {
		return m_materialPath;
	}

private:
	editor::ResourceSlot m_materialSlot { resource::ResourceType::MATERIAL };
	// editor::ResourceSlot m_shaderSlot{resource::ResourceType::SHADER};
	editor::ResourceSlot m_modelSlot { resource::ResourceType::MODEL };

	// std::string m_texturePath = "images/default.png";
	// std::string m_shaderPath = "SHADERS/default.shader";
	std::string m_meshPath = "MODELS/quad.obj";
	std::string m_materialPath = "MATERIALS/default.mat";

	// std::shared_ptr<Texture> m_texture;
	// std::shared_ptr<renderer::Shader> m_shader;
	std::shared_ptr<renderer::Mesh> m_mesh;
	std::shared_ptr<renderer::Material> m_material;
	std::shared_ptr<renderer::Shader> m_occlusionShader;
	std::shared_ptr<renderer::Shader> m_externalShader;
	std::shared_ptr<renderer::Shader> m_defaultShader;

	bool m_isOccluder = false;
	bool m_castsDirectionalShadow = true;
	bool m_isTransparent = false;

	bool m_drawToDepth = true;

	bool m_useExternalTexture = false;
	bool m_useExternalTextureOnly = false;
	unsigned int m_externalTextureId = 0;
	std::string m_externalTextureSampler = "gTexture";
	int m_externalTextureUnit = 0;

	bool m_isRegisteredInRenderer = false;

	// Per-instance vertex color (rgba) used when mesh doesn't provide per-vertex colors
	// glm::vec4 m_vertexColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
};
};
