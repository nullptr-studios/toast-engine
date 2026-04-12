/// @file MeshRendererComponent.cpp
/// @author dario
/// @date 28/09/2025.

#include "Toast/Profiler.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#include "imgui_stdlib.h"
#endif
#include "Toast/Components/MeshRendererComponent.hpp"
#include "Toast/Renderer/OclussionVolume.hpp"

namespace toast {

void MeshRendererComponent::RegisterWithRenderer(renderer::IRendererBase* renderer) {
	if (m_isTransparent) {
		renderer->AddTransparent(this);
	} else {
		renderer->AddRenderable(this);
	}
}

void MeshRendererComponent::UnregisterFromRenderer(renderer::IRendererBase* renderer) {
	if (m_isTransparent) {
		renderer->RemoveTransparent(this);
	} else {
		renderer->RemoveRenderable(this);
	}
}

void MeshRendererComponent::EnableInRenderer(renderer::IRendererBase* renderer) {
	if (m_isTransparent) {
		renderer->EnableTransparent(this);
	} else {
		renderer->EnableRenderable(this);
	}
}

void MeshRendererComponent::DisableInRenderer(renderer::IRendererBase* renderer) {
	if (m_isTransparent) {
		renderer->DisableTransparent(this);
	} else {
		renderer->DisableRenderable(this);
	}
}

void MeshRendererComponent::ApplyCustomUniforms(renderer::Shader* /*shader*/) noexcept { }

void MeshRendererComponent::SetTransparent(bool transparent) {
	if (m_isTransparent == transparent) {
		return;
	}

	auto* renderer = renderer::IRendererBase::GetInstance();
	if (renderer && m_isRegisteredInRenderer) {
		UnregisterFromRenderer(renderer);

		m_isTransparent = transparent;
		RegisterWithRenderer(renderer);
		return;
	}

	m_isTransparent = transparent;
}

void MeshRendererComponent::Load(json_t j, bool force_create) {
	PROFILE_ZONE_C(0x00FFFF);    // Cyan for deserialization
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

	if (j.contains("isOccluder")) {
		m_isOccluder = j.at("isOccluder").get<bool>();
	}
	if (j.contains("castsDirectionalShadow")) {
		m_castsDirectionalShadow = j.at("castsDirectionalShadow").get<bool>();
	}

	if (j.contains("drawToDepth")) {
		m_drawToDepth = j.at("drawToDepth").get<bool>();
	}

	if (j.contains("isTransparent")) {
		m_isTransparent = j.at("isTransparent").get<bool>();
	}
}

json_t MeshRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	// j["shaderPath"] = m_shaderPath;
	// j["texturePath"] = m_texturePath;
	j["meshPath"] = m_meshPath;
	// j["vertexColor"] = { m_vertexColor.r, m_vertexColor.g, m_vertexColor.b, m_vertexColor.a };
	j["materialPath"] = m_materialPath;
	j["isOccluder"] = m_isOccluder;
	j["castsDirectionalShadow"] = m_castsDirectionalShadow;
	j["drawToDepth"] = m_drawToDepth;
	j["isTransparent"] = m_isTransparent;
	return j;
}

#ifdef TOAST_EDITOR
void MeshRendererComponent::Inspector() {
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Indent(20);
		TransformComponent::Inspector();
		ImGui::Unindent(20);
	}

	ImGui::Checkbox("2D Light Occluder", &m_isOccluder);
	ImGui::Checkbox("Casts Directional Shadow", &m_castsDirectionalShadow);
	ImGui::Checkbox("Draw to depth", &m_drawToDepth);
	bool transparent = m_isTransparent;
	if (ImGui::Checkbox("Transparent", &transparent)) {
		SetTransparent(transparent);
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
	m_material = resource::LoadResource<renderer::Material>(m_materialPath);
	// m_texture = resource::ResourceManager::GetInstance()->LoadResource<Texture>(m_texturePath);
	m_mesh = resource::LoadResource<renderer::Mesh>(m_meshPath);
	m_occlusionShader = resource::LoadResource<renderer::Shader>("SHADERS/occlusion.shader");
	m_externalShader = resource::LoadResource<renderer::Shader>("SHADERS/default.shader");
	m_defaultShader = resource::LoadResource<renderer::Shader>("SHADERS/default.shader");

	SetRunTick(false);
	SetRunEarlyTick(false);
	SetRunLateTick(false);

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
	PROFILE_ZONE_C(0xFFFF00);    // Yellow for resource loading
	// opengl calls eso si que es en el main thread
	// m_shader->Use();
	// m_shader->SetSampler("Texture", 0);
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		RegisterWithRenderer(r);
		m_isRegisteredInRenderer = true;
	}
}

void MeshRendererComponent::OnRender(renderer::IRenderablePass pass, const glm::mat4& precomputed_mat) noexcept {
	if (!enabled()) {
		return;
	}

	if (m_mesh == nullptr) {
		return;
	}

	const bool external_only = m_useExternalTextureOnly && m_useExternalTexture && m_externalTextureId != 0;
	if (!external_only && m_material == nullptr) {
		return;
	}

	if (pass != renderer::IRenderablePass::DIRECTIONAL_SHADOW &&
	    !OclussionVolume::isTransformedAABBOnPlanes(m_mesh->boundingBox(), GetWorldMatrix())) {
		return;
	}

	// guard against null pointers (material or mesh might have failed to load)
	if (!external_only && m_material->GetShader() == nullptr) {
		return;
	}

	if (pass == renderer::IRenderablePass::OCCLUSION) {
		if (!m_isOccluder) {
			return;
		}
	}

	if (pass == renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		if (!m_castsDirectionalShadow) {
			return;
		}
	}

	PROFILE_ZONE;

	// compute transform once
	const glm::mat4 model = GetWorldMatrix();
	const glm::mat4 mvp = precomputed_mat * model;
	if (pass == renderer::IRenderablePass::GEOMETRY) {
		auto shader = external_only ? m_externalShader : m_material->GetShader();
		if (shader) {
			if (external_only || (m_useExternalTexture && m_externalTextureId != 0)) {
				shader->Use();
			} else {
				m_material->Use();
			}

			shader->Set("gWorld", model);

			// set generic transform uniform
			shader->Set("gMVP", mvp);

			if (external_only) {
				// default.shader uses gColor; keep neutral tint for HUD
				shader->Set("gColor", glm::vec4(1.0f));
			}

			if (m_useExternalTexture && m_externalTextureId != 0) {
				glActiveTexture(GL_TEXTURE0 + m_externalTextureUnit);
				glBindTexture(GL_TEXTURE_2D, m_externalTextureId);
				shader->SetSampler(m_externalTextureSampler, m_externalTextureUnit);
			}

			const bool receivesDirectionalShadows = (shader.get() == m_externalShader.get()) || (shader.get() == m_defaultShader.get());
			if (auto* renderer = renderer::IRendererBase::GetInstance(); renderer && receivesDirectionalShadows) {
				shader->Set("gDirectionalLightMatrix", renderer->GetDirectionalShadowMatrix());
				shader->Set("gDirectionalLightDir", renderer->GetGlobalLightDirection());
				shader->Set("gDirectionalShadowBias", renderer->GetDirectionalShadowBias());
				shader->Set("gDirectionalShadowStrength", renderer->GetDirectionalShadowStrength());
				shader->Set("gDirectionalShadowsEnabled", renderer->IsDirectionalShadowsEnabled() ? 1 : 0);

				glActiveTexture(GL_TEXTURE15);
				glBindTexture(GL_TEXTURE_2D, renderer->GetDirectionalShadowMapTexture());
				shader->SetSampler("gDirectionalShadowMap", 15);
				glActiveTexture(GL_TEXTURE0);
			}

			ApplyCustomUniforms(shader.get());
		}

		// draw

		// restore state
		// m_texture->Unbind();
		// m_shader->unuse();
	} else if (pass == renderer::IRenderablePass::OCCLUSION || pass == renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		// Ensure alpha-test shader samples a valid texture binding in depth-only passes.
		if (m_useExternalTexture && m_externalTextureId != 0) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_externalTextureId);
		} else if (!external_only && m_material) {
			m_material->Use();
		}

		m_occlusionShader->Use();
		m_occlusionShader->Set("gWorld", model);
		m_occlusionShader->SetSampler("Texture", 0);

		// set generic transform uniform
		m_occlusionShader->Set("gMVP", mvp);
	}

	if (pass == renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		// Shadow-map rendering must always write depth for valid caster silhouettes.
		glDepthMask(GL_TRUE);
	} else if (m_drawToDepth) {
		glDepthMask(GL_TRUE);
	} else {
		glDepthMask(GL_FALSE);
	}

	if (pass != renderer::IRenderablePass::OCCLUSION && pass != renderer::IRenderablePass::DIRECTIONAL_SHADOW) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glDisable(GL_BLEND);
	}

	m_mesh->Draw();
}

void MeshRendererComponent::Destroy() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		UnregisterFromRenderer(r);
	}
	m_isRegisteredInRenderer = false;
}

void MeshRendererComponent::OnEnable() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		EnableInRenderer(r);
	}
}

void MeshRendererComponent::OnDisable() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		DisableInRenderer(r);
	}
}
}
