/**
 * @file panel.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Viewport-space UI panel
 */

#pragma once
#include <memory>
#include <toast/ui/assets.hpp>
#include <toast/world/node.hpp>

namespace Rml {
class Context;
class ElementDocument;
}

namespace ui {
class UIBinds;
class UIBindStore;
}

namespace toast {

class TOAST_API [[ToastNode, Icon("Window"), Color("Blue")]] Panel : public Node {
public:
	Panel();
	~Panel() override;

	[[nodiscard]]
	auto rmlContext() -> Rml::Context* {
		return m_context;
	}

	[[nodiscard]]
	auto rmlDocument() -> Rml::ElementDocument* {
		return m_document;
	}

	[[nodiscard]]
	auto colorScheme() const -> const assets::Handle<assets::ColorScheme>& {
		return m_color_scheme;
	}

	void reloadDocument();

private:
	void onReflectedFieldChanged(std::string_view field_name) override;
	void init();
	void destroy();
	void onEnable();
	void onDisable();

	void loadDocument();
	void unloadDocument();

	[[Reflect]]
	assets::Handle<assets::UIElement> m_element;

	[[Reflect]]
	assets::Handle<assets::ColorScheme> m_color_scheme;

	[[Reflect]]
	std::vector<assets::Handle<assets::Font>> m_fonts;

	[[Reflect, Group("Data"), Name("Table")]]
	std::vector<assets::Handle<assets::Localization>> m_localizations;

	[[Reflect, Group("Data"), Name("Image table")]]
	std::vector<assets::Handle<assets::ImageLocalization>> m_image_localizations;

	Rml::Context* m_context = nullptr;
	Rml::ElementDocument* m_document = nullptr;
	std::unique_ptr<ui::UIBindStore> m_bind_store;
	std::unique_ptr<ui::UIBinds> m_binds;
};

}
