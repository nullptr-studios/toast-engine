/**
 * @file panel_context.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Holds long-lived global UI assets
 */

#pragma once
#include <toast/ui/assets.hpp>
#include <toast/world/node.hpp>

namespace toast {

class TOAST_API [[ToastNode, Color("Blue")]] PanelContext : public Node {
public:
	PanelContext() = default;

private:
	void onReflectedFieldChanged(std::string_view field_name) override;
	void init();
	void destroy();
	void registerAssets();
	void unregisterAssets();

	[[Reflect]]
	std::vector<assets::Handle<assets::Font>> m_fonts;

	[[Reflect]]
	std::vector<assets::Handle<assets::UIStyle>> m_styles;

	[[Reflect]]
	std::vector<assets::Handle<assets::ColorScheme>> m_color_schemes;

	[[Reflect]]
	std::vector<assets::Handle<assets::Localization>> m_localizations;

	[[Reflect]]
	std::vector<assets::Handle<assets::ImageLocalization>> m_image_localizations;

	std::vector<assets::Handle<assets::UIStyle>> m_registered_styles;
	std::vector<assets::Handle<assets::ColorScheme>> m_registered_color_schemes;
	std::vector<assets::Handle<assets::Localization>> m_registered_localizations;
	std::vector<assets::Handle<assets::ImageLocalization>> m_registered_image_localizations;
};

}
