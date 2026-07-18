#include "panel.hpp"

#include "../document_preprocess.hpp"
#include "../ui_binds.hpp"
#include "../ui_system.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

Panel::Panel() = default;
Panel::~Panel() = default;

void Panel::init() {
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

	// Viewport-sized context
	glm::ivec2 dims {1920, 1080};
	if (renderer::VulkanRenderer::instance) {
		const auto extent = renderer::VulkanRenderer::instance->getOutputTarget().getExtent();
		if (extent.width > 0 && extent.height > 0) {
			dims = {static_cast<int>(extent.width), static_cast<int>(extent.height)};
		}
	}

	m_context = ui.createContext(name(), dims);
	if (!m_context) {
		return;
	}

	ui.registerContextOwner(m_context, this);
	loadDocument();
	ui.registerPanel(this);
}

void Panel::destroy() {
	if (!ui::UISystem::exists()) {
		return;
	}
	auto& ui = ui::UISystem::get();

	ui.unregisterPanel(this);
	unloadDocument();

	if (m_context) {
		ui.unregisterContextOwner(m_context);
		ui.destroyContext(m_context);
		m_context = nullptr;
	}
}

void Panel::onEnable() {
	if (m_document) {
		m_document->Show();
	}
}

void Panel::onDisable() {
	if (m_document) {
		m_document->Hide();
	}
}

void Panel::loadDocument() {
	ZoneScoped;

	if (!m_context) {
		return;
	}

	if (!m_element.hasValue()) {
		TOAST_WARN("UI", "Panel '{}' has no UI element assigned", name());
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
		TOAST_ERROR("UI", "Panel '{}' failed to load document '{}'", name(), m_element.path());
		return;
	}

	if (enabled()) {
		m_document->Show();
	}

	TOAST_TRACE("UI", "Panel '{}' loaded '{}'", name(), m_element.path());
}

void Panel::unloadDocument() {
	if (m_context && m_document) {
		m_context->UnloadDocument(m_document);
		m_document = nullptr;
	}
	// Drop only after the document is gone
	m_binds.reset();
}

void Panel::reloadDocument() {
	unloadDocument();
	loadDocument();
}

}
