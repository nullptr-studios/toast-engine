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

	void OnRender(const glm::mat4& precomputed_mat) noexcept override;

	void Destroy() override;

	void SetMaterial(const std::string& path) {
		// m_materialSlot.SetResource(path);
		m_materialPath = path;
		m_material = resource::LoadResource<renderer::Material>(path);
	}

	[[nodiscard]]

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

	// Vertex color controls the generic vertex attribute (location = 3) used by the shaders
	//[[nodiscard]]
	// glm::vec4 GetVertexColor() const { return m_vertexColor; }

	// void SetVertexColor(const glm::vec4& c) { m_vertexColor = c; }

private:
	editor::ResourceSlot m_materialSlot { resource::ResourceType::MATERIAL };
	// editor::ResourceSlot m_shaderSlot{resource::ResourceType::SHADER};
	editor::ResourceSlot m_modelSlot { resource::ResourceType::MODEL };

	// std::string m_texturePath = "images/default.png";
	// std::string m_shaderPath = "shaders/default.shader";
	std::string m_meshPath = "models/quad.obj";
	std::string m_materialPath = "materials/default.mat";

	// std::shared_ptr<Texture> m_texture;
	// std::shared_ptr<renderer::Shader> m_shader;
	std::shared_ptr<renderer::Mesh> m_mesh;
	std::shared_ptr<renderer::Material> m_material;

	// Per-instance vertex color (rgba) used when mesh doesn't provide per-vertex colors
	// glm::vec4 m_vertexColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
};
};
