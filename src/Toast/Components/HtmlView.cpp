/// @file HtmlView.cpp
/// @brief HtmlView component implementation
/// @author Copilot
/// @date 07/03/2026

#include <Toast/Components/HtmlView.hpp>
#include <Toast/Localization.hpp>
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
	explicit HtmlViewListener(HtmlView::ConsoleCallback cb, ultralight::ViewListener* forward)
	    : m_cb(std::move(cb)), m_forward(forward) { }

	void OnChangeTitle(ultralight::View* caller, const ultralight::String& title) override {
		if (m_forward) {
			m_forward->OnChangeTitle(caller, title);
		}
	}

	void OnChangeURL(ultralight::View* caller, const ultralight::String& url) override {
		if (m_forward) {
			m_forward->OnChangeURL(caller, url);
		}
	}

	void OnChangeCursor(ultralight::View* caller, ultralight::Cursor cursor) override {
		if (m_forward) {
			m_forward->OnChangeCursor(caller, cursor);
		}
	}

	void OnAddConsoleMessage(ultralight::View* caller, const ultralight::ConsoleMessage& msg) override {
		std::string message = msg.message().utf8().data();
		TOAST_TRACE("[JS Console] {}", message);
		if (m_cb) {
			m_cb(message);
		}
		if (m_forward) {
			m_forward->OnAddConsoleMessage(caller, msg);
		}
	}

private:
	HtmlView::ConsoleCallback m_cb;
	ultralight::ViewListener* m_forward = nullptr;
};

class HtmlLoadListener : public ultralight::LoadListener {
public:
	explicit HtmlLoadListener(HtmlView::DOMReadyCallback cb, ultralight::LoadListener* forward)
	    : m_cb(std::move(cb)), m_forward(forward) { }

	void OnBeginLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (m_forward) {
			m_forward->OnBeginLoading(caller, frame_id, is_main_frame, url);
		}
	}

	void OnFinishLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (m_forward) {
			m_forward->OnFinishLoading(caller, frame_id, is_main_frame, url);
		}
	}

	void OnFailLoading(
	    ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url, const ultralight::String& description,
	    const ultralight::String& error_domain, int error_code
	) override {
		if (m_forward) {
			m_forward->OnFailLoading(caller, frame_id, is_main_frame, url, description, error_domain, error_code);
		}
	}

	void OnWindowObjectReady(
	    ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url
	) override {
		if (m_forward) {
			m_forward->OnWindowObjectReady(caller, frame_id, is_main_frame, url);
		}
	}

	void OnDOMReady(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (is_main_frame && m_cb) {
			m_cb();
		}
		if (m_forward) {
			m_forward->OnDOMReady(caller, frame_id, is_main_frame, url);
		}
	}

private:
	HtmlView::DOMReadyCallback m_cb;
	ultralight::LoadListener* m_forward = nullptr;
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
			m_viewListener = std::make_unique<HtmlViewListener>(m_consoleCb, m_view->view_listener());
			m_view->set_view_listener(m_viewListener.get());
		}
		if (m_domReadyCb) {
			m_loadListener = std::make_unique<HtmlLoadListener>(m_domReadyCb, m_view->load_listener());
			m_view->set_load_listener(m_loadListener.get());
		}
		m_view->LoadURL(ultralight::String(m_url.c_str()));
		hud->SetViewSortOrder(m_view, m_sortOrder);
		hud->ExecuteJS(Localization::BuildApplyScript());
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
	m_loadListener.reset();
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
	static std::string js;
	if (ImGui::InputText("Execute", &js, ImGuiInputTextFlags_EnterReturnsTrue)) {
		EvalJS(js);
	}
}
#endif

}    // namespace toast
