/// @file HtmlView.cpp
/// @brief HtmlView component implementation
/// @author Copilot
/// @date 07/03/2026

#include <Toast/Components/HtmlView.hpp>
#include <Toast/Log.hpp>
#include <Toast/Renderer/Framebuffer.hpp>
#include <Toast/Renderer/HUD/HUDLayer.hpp>

#ifdef TOAST_EDITOR
#include "imgui.h"
#include "imgui_stdlib.h"
#endif

namespace toast {

void HtmlView::Begin() {
	Component::Begin();
	CreateUlView();
}

void HtmlView::Destroy() {
	DestroyUlView();
}

void HtmlView::OnEnable() {
	CreateUlView();
}

void HtmlView::OnDisable() {
	DestroyUlView();
}

void HtmlView::CreateUlView() {
	if (m_view || m_url.empty()) {
		return;
	}

	auto* hud = renderer::HUD::HUDLayer::Get();
	if (!hud) {
		TOAST_ERROR("HtmlView: HUDLayer not available");
		return;
	}

	auto* fb = hud->GetFramebuffer();
	if (!fb) {
		TOAST_ERROR("HtmlView: HUDLayer framebuffer not ready");
		return;
	}

	m_view = hud->CreateView(fb->Width(), fb->Height());
	if (m_view) {
		m_view->LoadURL(ultralight::String(m_url.c_str()));
		TOAST_INFO("HtmlView: loaded {}", m_url);
	}
}

void HtmlView::DestroyUlView() {
	if (m_view) {
		if (auto* hud = renderer::HUD::HUDLayer::Get()) {
			hud->RemoveView(m_view);
		}
		m_view = nullptr;
	}
}

void HtmlView::SetUrl(const std::string& url) {
	m_url = url;

	// If already running, reload
	if (m_view) {
		m_view->LoadURL(ultralight::String(m_url.c_str()));
	}
}

json_t HtmlView::Save() const {
	json_t j = Component::Save();
	j["url"] = m_url;
	return j;
}

void HtmlView::Load(json_t j, bool force_create) {
	Component::Load(j, force_create);
	if (j.contains("url")) {
		m_url = j["url"].get<std::string>();
	}
}

#ifdef TOAST_EDITOR
void HtmlView::Inspector() {
	Component::Inspector();
	ImGui::SeparatorText("HtmlView");
	if (ImGui::InputText("URL", &m_url)) {
		if (m_view) {
			m_view->LoadURL(ultralight::String(m_url.c_str()));
		}
	}
}
#endif

}    // namespace toast
