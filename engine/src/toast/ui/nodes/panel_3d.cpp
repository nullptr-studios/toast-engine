#include "panel_3d.hpp"

#include "../document_preprocess.hpp"
#include "../ui_binds.hpp"
#include "../ui_system.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>

namespace toast {

Panel3D::Panel3D() = default;
Panel3D::~Panel3D() = default;

auto Panel3D::pixelSize() const -> glm::ivec2 {
	const glm::vec3 scale = worldScale();
	const int width = std::max(static_cast<int>(scale.x * m_pixels_per_meter), 1);
	const int height = std::max(static_cast<int>(scale.y * m_pixels_per_meter), 1);
	// Clamp to a sane texture ceiling
	return {std::min(width, 8192), std::min(height, 8192)};
}

void Panel3D::init() {
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

	m_context = ui.createContext(name(), pixelSize());
	if (!m_context) {
		return;
	}

	ui.registerContextOwner(m_context, this);
	loadDocument();
	ui.registerWorldPanel(this);
}

void Panel3D::destroy() {
	if (!ui::UISystem::exists()) {
		return;
	}
	auto& ui = ui::UISystem::get();

	ui.unregisterWorldPanel(this);
	unloadDocument();

	if (m_context) {
		ui.unregisterContextOwner(m_context);
		ui.destroyContext(m_context);
		m_context = nullptr;
	}
}

void Panel3D::onEnable() {
	if (m_document) {
		m_document->Show();
	}
}

void Panel3D::onDisable() {
	if (m_document) {
		m_document->Hide();
	}
}

void Panel3D::tick() {
	if (!m_context) {
		return;
	}

	// Follow world scale changes
	const glm::ivec2 size = pixelSize();
	const Rml::Vector2i dims = m_context->GetDimensions();
	if (size.x != dims.x || size.y != dims.y) {
		m_context->SetDimensions({size.x, size.y});
	}
}

void Panel3D::loadDocument() {
	ZoneScoped;

	if (!m_context) {
		return;
	}

	if (!m_element.hasValue()) {
		TOAST_WARN("UI", "Panel3D '{}' has no UI element assigned", name());
		return;
	}

	auto& ui = ui::UISystem::get();

	ui::PreprocessContext preprocess_ctx;
	preprocess_ctx.inject_data_model = true;
	preprocess_ctx.style_uris = ui.globalStyleUris();
	for (const auto& style : m_styles) {
		if (!style.path().empty()) {
			preprocess_ctx.style_uris.emplace_back(style.path());
		}
	}

	const auto scan = ui::preprocessDocument(m_element->source(), preprocess_ctx);

	if (!scan.events.empty() || !scan.binds.empty()) {
		m_binds = std::make_unique<ui::UIBinds>(m_context, this, scan);
	}

	m_document = m_context->LoadDocumentFromMemory(scan.transformed_rml, Rml::String(m_element.path()));
	if (!m_document) {
		TOAST_ERROR("UI", "Panel3D '{}' failed to load document '{}'", name(), m_element.path());
		return;
	}

	if (enabled()) {
		m_document->Show();
	}

	TOAST_TRACE("UI", "Panel3D '{}' loaded '{}'", name(), m_element.path());
}

void Panel3D::unloadDocument() {
	if (m_context && m_document) {
		m_context->UnloadDocument(m_document);
		m_document = nullptr;
	}
	// Drop only after the document is gone
	m_binds.reset();
}

void Panel3D::reloadDocument() {
	unloadDocument();
	loadDocument();
}

}
