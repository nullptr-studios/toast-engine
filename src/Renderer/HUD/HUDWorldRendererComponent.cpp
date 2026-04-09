/// @file HUDWorldRendererComponent.cpp

#include "Toast/Renderer/HUD/HUDWorldRendererComponent.hpp"

#include "Toast/Renderer/HUD/HUDLayer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Log.hpp"

namespace {

std::string ResolveWorldHudShaderPath() {
	constexpr const char* kWorldHudShader = "SHADERS/world_hud.shader";
	constexpr const char* kWorldHudShaderLower = "shaders/world_hud.shader";
	constexpr const char* kDefaultShader = "SHADERS/default.shader";

	if (resource::Open(kWorldHudShader).has_value()) {
		return kWorldHudShader;
	}
	if (resource::Open(kWorldHudShaderLower).has_value()) {
		return kWorldHudShaderLower;
	}

	TOAST_WARN("HUDWorldRendererComponent: world_hud shader not found in active assets, falling back to default.shader");
	return kDefaultShader;
}

}

namespace toast {

void HUDWorldRendererComponent::Init() {
	TransformComponent::Init();

	m_mesh = resource::LoadResource<renderer::Mesh>(m_meshPath);
	m_shader = resource::LoadResource<renderer::Shader>(ResolveWorldHudShaderPath());

	SetRunEarlyTick(false);
	SetRunTick(false);
	SetRunLateTick(false);
}

void HUDWorldRendererComponent::LoadTextures() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		r->AddTransparent(this);
		m_registered = true;
	}
}

void HUDWorldRendererComponent::OnRender(renderer::IRenderablePass pass, const glm::mat4& precomputed_mat) noexcept {
	if (!enabled() || pass != renderer::IRenderablePass::GEOMETRY) {
		return;
	}
	if (!m_mesh || !m_shader) {
		return;
	}

	if (m_view && m_hudLayer) {
		m_textureId = m_hudLayer->GetViewTextureGL(m_view);
	}
	if (m_textureId == 0) {
		return;
	}

	const glm::mat4 model = GetWorldMatrix();
	const glm::mat4 mvp = precomputed_mat * model;

	m_shader->Use();
	m_shader->Set("gWorld", model);
	m_shader->Set("gMVP", mvp);
	m_shader->Set("gColor", glm::vec4(1.0f));

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_textureId);
	m_shader->SetSampler("gTexture", 0);

	glDepthMask(m_drawToDepth ? GL_TRUE : GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_mesh->Draw();
}

void HUDWorldRendererComponent::Destroy() {
	if (m_registered) {
		if (auto* r = renderer::IRendererBase::GetInstance()) {
			r->RemoveTransparent(this);
		}
		m_registered = false;
	}
}

void HUDWorldRendererComponent::OnEnable() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		r->EnableTransparent(this);
	}
}

void HUDWorldRendererComponent::OnDisable() {
	if (auto* r = renderer::IRendererBase::GetInstance()) {
		r->DisableTransparent(this);
	}
}

void HUDWorldRendererComponent::Load(json_t j, bool force_create) {
	TransformComponent::Load(j, force_create);
	if (j.contains("meshPath")) {
		m_meshPath = j.at("meshPath").get<std::string>();
	}
	if (j.contains("drawToDepth")) {
		m_drawToDepth = j.at("drawToDepth").get<bool>();
	}
}

json_t HUDWorldRendererComponent::Save() const {
	json_t j = TransformComponent::Save();
	j["meshPath"] = m_meshPath;
	j["drawToDepth"] = m_drawToDepth;
	return j;
}

}



