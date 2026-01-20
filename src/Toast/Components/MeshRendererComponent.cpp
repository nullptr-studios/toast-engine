/// @file MeshRendererComponent.cpp
/// @author dario
/// @date 28/09/2025.

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#include "imgui_stdlib.h"
#endif
#include "Toast/Components/MeshRendererComponent.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"

namespace toast {

void MeshRendererComponent::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	// if (j.contains("shaderPath")) {
	//	m_shaderPath = j.at("shaderPath");
	// }
	// if (j.contains("texturePath")) {
	//	m_texturePath = j.at("texturePath");
	// }
	if (j.contains("meshPath")) {
		m_meshPath = j.at("meshPath");
	}
	// if (j.contains("vertexColor")) {
	//	auto arr = j.at("vertexColor");
	//	m_vertexColor = glm::vec4(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>(), arr[3].get<float>());
	// }

	if (j.contains("materialPath")) {
		m_materialPath = j.at("materialPath").get<std::string>();
	}
}

json_t MeshRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	// j["shaderPath"] = m_shaderPath;
	// j["texturePath"] = m_texturePath;
	j["meshPath"] = m_meshPath;
	// j["vertexColor"] = { m_vertexColor.r, m_vertexColor.g, m_vertexColor.b, m_vertexColor.a };
	j["materialPath"] = m_materialPath;
	return j;
}

#ifdef TOAST_EDITOR
void MeshRendererComponent::Inspector() {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		TransformComponent::Inspector();
		ImGui::Unindent(20);
	}

	ImGui::Spacing();
	// Vertex color picker
	// ImGui::ColorEdit4("Vertex Color", &m_vertexColor.r);

	// use renamed resource slot members
	m_materialSlot.Show();

	// m_shaderSlot.Show();
	m_modelSlot.Show();
}

#endif
void MeshRendererComponent::Init() {
	TransformComponent::Init();
	// init just for loading
	m_material = resource::ResourceManager::GetInstance()->LoadResource<renderer::Material>(m_materialPath);
	// m_texture = resource::ResourceManager::GetInstance()->LoadResource<Texture>(m_texturePath);
	m_mesh = resource::ResourceManager::GetInstance()->LoadResource<renderer::Mesh>(m_meshPath);

#ifdef TOAST_EDITOR
	m_materialSlot.SetOnDroppedLambda([this](const std::string& p) {
		SetMaterial(p);
	});
	// m_textureSlot.name("Texture");
	// m_shaderSlot.SetOnDroppedLambda([this](const std::string& p) {
	// 	SetShader(p);
	// });
	// m_shaderSlot.name("Shader");
	m_modelSlot.SetOnDroppedLambda([this](const std::string& p) {
		SetMesh(p);
	});
	m_modelSlot.name("Model");

	// Ensure the editor slot always knows the stored path, even if LoadResource failed
	m_materialSlot.SetInitialResource(m_materialPath);
	// m_shaderSlot.SetInitialResource(m_shaderPath);
	m_modelSlot.SetInitialResource(m_meshPath);
#endif
}

void MeshRendererComponent::LoadTextures() {
	// opengl calls eso si que es en el main thread
	// m_shader->Use();
	// m_shader->SetSampler("Texture", 0);
	renderer::IRendererBase::GetInstance()->AddRenderable(this);
}

void MeshRendererComponent::OnRender(const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (!OclussionVolume::isTransformedAABBOnPlanes(
		renderer::IRendererBase::GetInstance()->GetFrustumPlanes(), 
		m_mesh->boundingBox(), 
		GetWorldMatrix())) 
		{ 	
		return;
	}

	// guard against null pointers (material or mesh might have failed to load)
	if (m_material == nullptr) {
		return;
	}
	if (m_material->GetShader() == nullptr || m_mesh == nullptr) {
		return;
	}

	PROFILE_ZONE;

	// compute transform once
	const glm::mat4 model = GetWorldMatrix();
	const glm::mat4 mvp = precomputed_mat * model;

	m_material->Use();
	auto shader = m_material->GetShader();
	if (shader) {
		// upload world matrix for deferred / lighting passes
		shader->Set("gWorld", model);

		// set generic transform uniform
		shader->Set("gMVP", mvp);
	}

	// draw
	m_mesh->Draw();

	// restore state
	// m_texture->Unbind();
	// m_shader->unuse();
}

void MeshRendererComponent::Destroy() {
	renderer::IRendererBase::GetInstance()->RemoveRenderable(this);
}

}
