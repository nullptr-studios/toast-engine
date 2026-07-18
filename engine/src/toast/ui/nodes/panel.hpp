/**
 * @file panel.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Viewport-space UI panel
 */

#pragma once
#include <toast/ui/assets.hpp>
#include <toast/world/node.hpp>

namespace Rml {
class Context;
class ElementDocument;
}

namespace toast {

class TOAST_API [[ToastNode, Icon("Window"), Color("Blue")]] Panel : public Node {
public:
	Panel() = default;

	[[nodiscard]]
	auto rmlContext() -> Rml::Context* {
		return m_context;
	}

	[[nodiscard]]
	auto rmlDocument() -> Rml::ElementDocument* {
		return m_document;
	}

	[[nodiscard]]
	auto colorScheme() const -> const assets::AssetHandle<assets::ColorScheme>& {
		return m_color_scheme;
	}

	void reloadDocument();

private:
	void init();
	void destroy();
	void onEnable();
	void onDisable();

	void loadDocument();
	void unloadDocument();

	[[Reflect]]
	assets::AssetHandle<assets::UIElement> m_element;

	[[Reflect]]
	std::vector<assets::AssetHandle<assets::UIStyle>> m_styles;

	[[Reflect]]
	std::vector<assets::AssetHandle<assets::Font>> m_fonts;

	[[Reflect]]
	std::vector<assets::AssetHandle<assets::FontFamily>> m_font_families;

	[[Reflect]]
	assets::AssetHandle<assets::ColorScheme> m_color_scheme;

	Rml::Context* m_context = nullptr;
	Rml::ElementDocument* m_document = nullptr;
};

}
