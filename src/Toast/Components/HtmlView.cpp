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

/// Per-view listener that forwards console messages to a callback
class HtmlViewListener : public ultralight::ViewListener {
public:
	explicit HtmlViewListener(HtmlView::ConsoleCallback cb) : m_cb(std::move(cb)) { }

	void OnAddConsoleMessage(ultralight::View* caller, const ultralight::ConsoleMessage& msg) override {
		std::string message = msg.message().utf8().data();
		TOAST_TRACE("[JS Console] {}", message);
		if (m_cb) {
			m_cb(message);
		}
	}

private:
	HtmlView::ConsoleCallback m_cb;
};

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
		// If a console callback is set, install a per-view listener
		if (m_consoleCb) {
			m_viewListener = std::make_unique<HtmlViewListener>(m_consoleCb);
			m_view->set_view_listener(m_viewListener.get());
		}
		m_view->LoadURL(ultralight::String(m_url.c_str()));
		hud->SetViewSortOrder(m_view, m_sortOrder);
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
	m_viewListener.reset();
}

void HtmlView::SetUrl(const std::string& url) {
	m_url = url;

	// If already running, reload
	if (m_view) {
		m_view->LoadURL(ultralight::String(m_url.c_str()));
	}
}

void HtmlView::SetSortOrder(int order) {
	m_sortOrder = order;
	if (m_view) {
		if (auto* hud = renderer::HUD::HUDLayer::Get()) {
			hud->SetViewSortOrder(m_view, m_sortOrder);
		}
	}
}

void HtmlView::EvalJS(const std::string& script) {
	if (!m_view) {
		return;
	}
	m_view->EvaluateScript(ultralight::String(script.c_str()));
}

json_t HtmlView::Save() const {
	json_t j = Component::Save();
	j["url"] = m_url;
	j["sort_order"] = m_sortOrder;
	return j;
}

void HtmlView::Load(json_t j, bool force_create) {
	Component::Load(j, force_create);
	if (j.contains("url")) {
		m_url = j["url"].get<std::string>();
	}
	if (j.contains("sort_order")) {
		m_sortOrder = j["sort_order"].get<int>();
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
	if (ImGui::DragInt("Sort Order", &m_sortOrder)) {
		SetSortOrder(m_sortOrder);
	}
}
#endif

}    // namespace toast
