/// @file HUDActor.cpp
/// @author dario
/// @date 08/04/2026.

#include "Toast/Renderer/HUD/HUDActor.hpp"

#include "Toast/Renderer/HUD/HUDLayer.hpp"
#include "Toast/Renderer/HUD/HUDWorldRendererComponent.hpp"
#include "Toast/Resources/ResourceManager.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#include "imgui_stdlib.h"
#endif

namespace {
std::string ResolveExistingPath(const std::string& preferred, const std::string& fallback_upper, const std::string& fallback_lower) {
	if (!preferred.empty() && resource::Open(preferred).has_value()) {
		return preferred;
	}
	if (resource::Open(fallback_upper).has_value()) {
		return fallback_upper;
	}
	if (resource::Open(fallback_lower).has_value()) {
		return fallback_lower;
	}
	return preferred.empty() ? fallback_upper : preferred;
}

constexpr float kMinPixelToUnitScale = 0.0001f;
constexpr float kMaxPixelToUnitScale = 1.0f;
}

void HUDActor::EnsureRendererComponent() {
	if (!m_worldRenderer) {
		m_worldRenderer = children.AddRequired<toast::HUDWorldRendererComponent>("HUDWorldRenderer");
	}

	if (!m_worldRenderer) {
		return;
	}
	m_worldRenderer->SetDrawToDepth(false);
}

void HUDActor::Init() {
	toast::Actor::Init();
	EnsureRendererComponent();

	if (m_worldRenderer) {
		m_meshPath = ResolveExistingPath(m_meshPath, "MODELS/quad.obj", "models/quad.obj");
		m_worldRenderer->SetMeshPath(m_meshPath);
	}
}

void HUDActor::Begin() {
	toast::Actor::Begin();
	EnsureRendererComponent();
	CreateView();
	SyncResolvedTexture();
	ApplyMeshScaleFromViewSize();
	
}

void HUDActor::Tick() {
	EnsureRendererComponent();
	if (!m_view) {
		CreateView();
	}
	SyncResolvedTexture();
}

void HUDActor::Destroy() {
	DestroyView();
}

void HUDActor::OnEnable() {
	CreateView();
}

void HUDActor::OnDisable() {
	DestroyView();
}

json_t HUDActor::Save() const {
	json_t j = toast::Actor::Save();
	j["url"] = m_url;
	j["viewWidth"] = m_viewWidth;
	j["viewHeight"] = m_viewHeight;
	j["scaleMeshByViewSize"] = m_scaleMeshByViewSize;
	j["pixelToUnitScale"] = m_pixelToUnitScale;
	j["sortOrder"] = m_sortOrder;
	j["meshPath"] = m_meshPath;
	j["materialPath"] = m_materialPath;
	return j;
}

void HUDActor::Load(json_t j, bool force_create) {
	if (j.contains("url")) {
		m_url = j.at("url").get<std::string>();
	}
	if (j.contains("viewWidth")) {
		m_viewWidth = j.at("viewWidth").get<uint32_t>();
	}
	if (j.contains("viewHeight")) {
		m_viewHeight = j.at("viewHeight").get<uint32_t>();
	}
	if (j.contains("scaleMeshByViewSize")) {
		m_scaleMeshByViewSize = j.at("scaleMeshByViewSize").get<bool>();
	}
	if (j.contains("pixelToUnitScale")) {
		m_pixelToUnitScale = j.at("pixelToUnitScale").get<float>();
	}
	m_pixelToUnitScale = glm::clamp(m_pixelToUnitScale, kMinPixelToUnitScale, kMaxPixelToUnitScale);
	if (j.contains("sortOrder")) {
		m_sortOrder = j.at("sortOrder").get<int>();
	}
	if (j.contains("meshPath")) {
		m_meshPath = j.at("meshPath").get<std::string>();
	}
	if (j.contains("materialPath")) {
		m_materialPath = j.at("materialPath").get<std::string>();
	}

	toast::Actor::Load(j, force_create);
	EnsureRendererComponent();
	if (m_worldRenderer) {
		m_meshPath = ResolveExistingPath(m_meshPath, "MODELS/quad.obj", "models/quad.obj");
		m_worldRenderer->SetMeshPath(m_meshPath);
	}
	ApplyMeshScaleFromViewSize();
}

void HUDActor::SetUrl(const std::string& url) {
	m_url = url;
	if (!m_view) {
		CreateView();
	}
	if (m_view && !m_url.empty()) {
		m_view->LoadURL(ultralight::String(m_url.c_str()));
	}
}

void HUDActor::ExecuteJS(const std::string& script) {
	if (m_view) {
		m_view->EvaluateScript(ultralight::String(script.c_str()));
		return;
	}
	m_pendingScripts.push_back(script);
}

#ifdef TOAST_EDITOR
void HUDActor::Inspector() {
	toast::Actor::Inspector();
	EnsureRendererComponent();

	ImGui::SeparatorText("HUDActor");
	ImGui::Text("View: %s", m_view ? "Ready" : "Not created");
	if (auto* hud = renderer::HUD::HUDLayer::Get()) {
		const uint32_t tex = m_view ? hud->GetViewTextureGL(m_view) : 0;
		ImGui::Text("Resolved Texture: %u", tex);
	} else {
		ImGui::TextColored(ImVec4(1.f, 0.7f, 0.2f, 1.f), "HUDLayer not available");
	}

	if (ImGui::InputText("URL", &m_url)) {
		SetUrl(m_url);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload URL")) {
		SetUrl(m_url);
	}

	int width = static_cast<int>(m_viewWidth);
	int height = static_cast<int>(m_viewHeight);
	bool recreate_view = false;
	if (ImGui::DragInt("View Width", &width, 1.0f, 1, 16384)) {
		m_viewWidth = static_cast<uint32_t>(width);
		ApplyMeshScaleFromViewSize();
		recreate_view = true;
	}
	if (ImGui::DragInt("View Height", &height, 1.0f, 1, 16384)) {
		m_viewHeight = static_cast<uint32_t>(height);
		ApplyMeshScaleFromViewSize();
		recreate_view = true;
	}

	if (ImGui::Checkbox("Scale Mesh By View Size", &m_scaleMeshByViewSize)) {
		ApplyMeshScaleFromViewSize();
	}
	if (ImGui::DragFloat("Pixel To Unit Scale", &m_pixelToUnitScale, 0.0005f, kMinPixelToUnitScale, kMaxPixelToUnitScale, "%.4f")) {
		m_pixelToUnitScale = glm::clamp(m_pixelToUnitScale, kMinPixelToUnitScale, kMaxPixelToUnitScale);
		ApplyMeshScaleFromViewSize();
	}

	if (ImGui::DragInt("Sort Order", &m_sortOrder)) {
		if (m_view) {
			if (auto* hud = renderer::HUD::HUDLayer::Get()) {
				hud->SetViewSortOrder(m_view, m_sortOrder);
			}
		}
	}

	if (recreate_view) {
		DestroyView();
		CreateView();
	}

	if (ImGui::Button("Recreate View")) {
		DestroyView();
		CreateView();
	}
}
#endif

void HUDActor::CreateView() {
	if (m_view) {
		return;
	}

	auto* hud = renderer::HUD::HUDLayer::Get();
	if (!hud) {
		return;
	}

	// World HUD depends on HUDLayer's Ultralight update/render loop.
	hud->Enable();

	ultralight::ViewConfig cfg;
	cfg.is_accelerated = true;
	cfg.is_transparent = true;
	cfg.enable_images = true;
	cfg.enable_javascript = true;
	cfg.initial_focus = false;

	m_view = hud->CreateView(m_viewWidth, m_viewHeight, cfg, false);
	if (!m_view) {
		return;
	}
	ApplyMeshScaleFromViewSize();
	SyncResolvedTexture();

	hud->SetViewSortOrder(m_view, m_sortOrder);
	if (!m_url.empty()) {
		m_view->LoadURL(ultralight::String(m_url.c_str()));
	}

	if (!m_pendingScripts.empty()) {
		for (const auto& script : m_pendingScripts) {
			m_view->EvaluateScript(ultralight::String(script.c_str()));
		}
		m_pendingScripts.clear();
	}
}

void HUDActor::DestroyView() {
	if (!m_view) {
		return;
	}

	if (auto* hud = renderer::HUD::HUDLayer::Get()) {
		hud->RemoveView(m_view);
	}
	m_view = nullptr;

	if (m_worldRenderer) {
		m_worldRenderer->ClearView();
	}
}

void HUDActor::SyncResolvedTexture() {
	if (!m_worldRenderer) {
		return;
	}

	auto* hud = renderer::HUD::HUDLayer::Get();
	if (!hud) {
		m_worldRenderer->ClearView();
		return;
	}
	m_worldRenderer->SetView(m_view, hud);
}

void HUDActor::ApplyMeshScaleFromViewSize() {
	if (!m_scaleMeshByViewSize) {
		return;
	}
	m_pixelToUnitScale = glm::clamp(m_pixelToUnitScale, kMinPixelToUnitScale, kMaxPixelToUnitScale);

	// quad.obj spans 2x2 units, so use half extents when mapping view pixels to world units.
	const float width_units = static_cast<float>(m_viewWidth) * 0.5f * m_pixelToUnitScale;
	const float height_units = static_cast<float>(m_viewHeight) * 0.5f * m_pixelToUnitScale;
	transform()->scale(glm::vec3(width_units, height_units, 1.0f));
}

void HUDActor::SetDimestion(uint32_t width, uint32_t height) {
	m_viewWidth = width;
	m_viewHeight = height;
	ApplyMeshScaleFromViewSize();
	DestroyView();
	CreateView();
}
