/// @file HtmlView.hpp
/// @brief Component that creates and manages an Ultralight HTML view
/// @author Copilot
/// @date 07/03/2026

#pragma once

#include "Toast/Components/Component.hpp"

#include <Ultralight/Ultralight.h>
#include <functional>
#include <memory>
#include <string>

namespace toast {

/// @class toast::HtmlView
/// @brief Component that loads and displays an HTML page via Ultralight
///
/// Add this component to any Object to render an HTML UI view.
/// The URL is serializable so it persists in scene files.
///
/// @par Usage:
/// @code
/// auto* view = children.Add<toast::HtmlView>("MyView");
/// view->SetUrl("file:///assets/UI/menus/MainMenu.html");
/// @endcode
class HtmlView : public Component {
public:
	REGISTER_TYPE(HtmlView);

	using ConsoleCallback = std::function<void(const std::string&)>;

	void Begin() override;

	void SetUrl(const std::string& url);

	const std::string& GetUrl() const {
		return m_url;
	}

	void SetConsoleCallback(ConsoleCallback cb) {
		m_consoleCb = std::move(cb);
	}

	ultralight::RefPtr<ultralight::View> GetView() const {
		return m_view;
	}

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif

protected:
	void Destroy() override;
	void OnEnable() override;
	void OnDisable() override;

private:
	void CreateUlView();
	void DestroyUlView();

	std::string m_url;
	ultralight::RefPtr<ultralight::View> m_view;
	ConsoleCallback m_consoleCb;
	std::unique_ptr<ultralight::ViewListener> m_viewListener;
};

}    // namespace toast
