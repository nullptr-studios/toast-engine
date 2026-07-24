#include "panel_context.hpp"

#include "../ui_system.hpp"

namespace toast {

void PanelContext::registerAssets() {
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
	m_registered_styles = m_styles;
	m_registered_color_schemes = m_color_schemes;
	m_registered_localizations = m_localizations;
	m_registered_image_localizations = m_image_localizations;
}

void PanelContext::unregisterAssets() {
	if (!ui::UISystem::exists()) {
		return;
	}
	auto& ui = ui::UISystem::get();
	for (const auto& style : m_registered_styles) {
		ui.unregisterGlobalStyle(style);
	}
	for (const auto& scheme : m_registered_color_schemes) {
		ui.unregisterGlobalScheme(scheme);
	}
	for (const auto& loc : m_registered_localizations) {
		ui.unregisterGlobalLocalization(loc);
	}
	for (const auto& loc : m_registered_image_localizations) {
		ui.unregisterGlobalImageLocalization(loc);
	}
	m_registered_styles.clear();
	m_registered_color_schemes.clear();
	m_registered_localizations.clear();
	m_registered_image_localizations.clear();
}

void PanelContext::onReflectedFieldChanged(std::string_view field_name) {
	if (!ui::UISystem::exists()) {
		return;
	}
	if (field_name == "m_fonts") {
		for (const auto& font : m_fonts) {
			ui::UISystem::get().loadFontFace(font);
		}
	} else if (field_name == "m_styles" || field_name == "m_color_schemes" || field_name == "m_localizations" ||
	           field_name == "m_image_localizations") {
		unregisterAssets();
		registerAssets();
	}
	ui::UISystem::get().reloadAllDocuments();
}

void PanelContext::init() {
	ZoneScoped;
	registerAssets();

	TOAST_INFO(
	    "UI", "PanelContext registered {} fonts, {} styles, {} schemes", m_fonts.size(), m_styles.size(), m_color_schemes.size()
	);
}

void PanelContext::destroy() {
	// fonts stay loaded as RmlUi has no font unloading
	unregisterAssets();
}

}
