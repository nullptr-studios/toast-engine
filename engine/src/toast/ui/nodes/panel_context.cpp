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
	for (const auto& style : m_styles) {
		ui.registerGlobalStyle(style);
	}
	for (const auto& scheme : m_color_schemes) {
		ui.registerGlobalScheme(scheme);
	}
	for (const auto& loc : m_localizations) {
		ui.registerGlobalLocalization(loc);
	}
	for (const auto& loc : m_image_localizations) {
		ui.registerGlobalImageLocalization(loc);
	}

	TOAST_INFO(
	    "UI", "PanelContext registered {} fonts, {} styles, {} schemes", m_fonts.size(), m_styles.size(), m_color_schemes.size()
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
	for (const auto& loc : m_localizations) {
		ui.unregisterGlobalLocalization(loc);
	}
	for (const auto& loc : m_image_localizations) {
		ui.unregisterGlobalImageLocalization(loc);
	}
}

}
