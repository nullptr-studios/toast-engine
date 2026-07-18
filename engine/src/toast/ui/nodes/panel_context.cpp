#include "panel_context.hpp"

#include "../ui_system.hpp"

namespace toast {

void PanelContext::init() {
	ZoneScoped;

	if (!ui::UISystem::exists()) {
		return;
	}
	auto& ui = ui::UISystem::get();

	for (const auto& font : m_fonts) {
		ui.loadFontFace(font);
	}
	for (const auto& family : m_font_families) {
		ui.loadFontFamily(family);
	}
	for (const auto& style : m_styles) {
		ui.registerGlobalStyle(style);
	}
	for (const auto& scheme : m_color_schemes) {
		ui.registerGlobalScheme(scheme);
	}

	TOAST_INFO(
	    "UI",
	    "PanelContext registered {} fonts, {} families, {} styles, {} schemes",
	    m_fonts.size(),
	    m_font_families.size(),
	    m_styles.size(),
	    m_color_schemes.size()
	);
}

void PanelContext::destroy() {
	if (!ui::UISystem::exists()) {
		return;
	}
	auto& ui = ui::UISystem::get();

	// fonts stay loaded as RmlUi has no font unloading
	for (const auto& style : m_styles) {
		ui.unregisterGlobalStyle(style);
	}

	for (const auto& scheme : m_color_schemes) {
		ui.unregisterGlobalScheme(scheme);
	}
}

}
