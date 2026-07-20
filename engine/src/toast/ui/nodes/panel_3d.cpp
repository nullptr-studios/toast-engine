#include "panel_3d.hpp"

#include "../document_preprocess.hpp"
#include "../localization_apply.hpp"
#include "../ui_binds.hpp"
#include "../ui_system.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>

namespace toast {

Panel3D::Panel3D() {
	m_bind_store = std::make_unique<ui::UIBindStore>();
	m_element.onChangeCallback([this] { reloadDocument(); });
	m_color_scheme.onChangeCallback([this] { reloadDocument(); });
}

Panel3D::~Panel3D() = default;

auto Panel3D::buildLocalizationScope() const -> ui::UISystem::LocalizationScope {
	ui::UISystem::LocalizationScope scope;
	scope.color_scheme = m_color_scheme.hasValue() ? m_color_scheme.operator->() : nullptr;
	for (const auto& loc : m_localizations) {
		if (loc.hasValue()) {
			scope.texts.push_back(loc.operator->());
		}
	}
	for (const auto& loc : m_image_localizations) {
		if (loc.hasValue()) {
			scope.images.push_back(loc.operator->());
		}
	}
	return scope;
}

auto Panel3D::pixelSize() const -> glm::ivec2 {
	const glm::vec3 scale = world_scale;
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
	ui.pushLocalizationScope(buildLocalizationScope());

	ui::PreprocessContext preprocess_ctx;
	preprocess_ctx.inject_data_model = true;
	preprocess_ctx.style_uris = ui.globalStyleUris();
	preprocess_ctx.color_resolver = [&ui](std::string_view name) { return ui.colorHex(name); };

	const auto scan = ui::preprocessDocument(m_element->source(), preprocess_ctx);
	m_bind_store->reconcile(scan);

	if (!scan.events.empty() || !scan.binds.empty()) {
		m_binds = std::make_unique<ui::UIBinds>(m_context, this, scan, *m_bind_store);
	}

	// Localization scope must be active so TranslateString and localized images resolve
	m_document = m_context->LoadDocumentFromMemory(scan.transformed_rml, Rml::String(m_element.path()));
	if (m_document) {
		ui::applyImageLocalization(m_document);
	}
	ui.popLocalizationScope();

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
