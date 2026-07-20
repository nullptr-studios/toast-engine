#include "ui_system.hpp"

#include "nodes/panels.hpp"
#include "render/rmlui_renderer_vk.h"
#include "render/ui_pass.hpp"
#include "text_format.hpp"
#include "ui_binds.hpp"
#include "ui_event_listener.hpp"
#include "ui_file_interface.hpp"
#include "ui_input.hpp"
#include "ui_system_interface.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Factory.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/project_settings.hpp>
#include <toast/renderer/vulkan_core.hpp>
#include <tracy/Tracy.hpp>

namespace ui {

UISystem::UISystem() noexcept {
	ZoneScoped;
	TOAST_ASSERT(not instance, "UI", "UISystem already exists");
	instance = this;

	m_file_interface = std::make_unique<UIFileInterface>([this](std::string_view name) { return colorHex(name); });
	m_system_interface = std::make_unique<UISystemInterface>();

	Rml::SetFileInterface(m_file_interface.get());
	Rml::SetSystemInterface(m_system_interface.get());

	if (!Rml::Initialise()) {
		TOAST_ERROR("UI", "RmlUi failed to initialise");
		return;
	}

	m_input_router = std::make_unique<UIInputRouter>();
	installEventListenerInstancer();
	m_asset_listener.subscribe<event::UIAssetReloaded>([this](const event::UIAssetReloaded& e) {
		m_documents_reload_pending = true;
		m_stylesheet_cache_dirty = m_stylesheet_cache_dirty || e.type == "ui_style" || e.type == "color_scheme";
		TOAST_INFO("UI", "Queued document reload after {} changed ({})", e.type, e.uid);
	});

	TOAST_INFO("UI", "RmlUi {} initialised", Rml::GetVersion());
}

UISystem::~UISystem() noexcept {
	// RmlUi releases its textures through the render interface
	// We need to kill it first
	Rml::Shutdown();

	if (m_render_interface) {
		m_render_interface->Shutdown();
		m_render_interface.reset();
	}

	instance = nullptr;
}

void UISystem::initializeRenderer(const renderer::VulkanCore& core) {
	ZoneScoped;
	TOAST_ASSERT(!m_render_interface, "UI", "UI render backend already initialized");

	const vk::Format stencil_format = UIPass::selectStencilFormat(core);

	m_render_interface = std::make_unique<RenderInterface_VK>();
	const bool ok = m_render_interface->Initialize(
	    static_cast<VkInstance>(*core.getInstance()),
	    static_cast<VkPhysicalDevice>(*core.getPhysicalDevice()),
	    static_cast<VkDevice>(*core.getDevice()),
	    core.getGraphicsQueueFamilyIndex(),
	    static_cast<VkQueue>(core.getGraphicsQueue()),
	    static_cast<VkFormat>(UIPass::k_color_format),
	    static_cast<VkFormat>(stencil_format),
	    &core.graphicsSubmitMutex()
	);

	if (!ok) {
		TOAST_ERROR("UI", "Failed to initialize the RmlUi Vulkan backend");
		m_render_interface.reset();
		return;
	}

	TOAST_INFO("UI", "RmlUi Vulkan backend initialized");
}

void UISystem::buildDrawFrame(renderer::VulkanRenderer::RenderFrame& frame) {
	ZoneScopedN("UISystem::buildDrawFrame");

	if (!m_render_interface) {
		return;
	}

	// Keep viewport panels sized to the output target
	Rml::Vector2i viewport_dims {0, 0};
	if (renderer::VulkanRenderer::instance) {
		const auto extent = renderer::VulkanRenderer::instance->getOutputTarget().getExtent();
		viewport_dims = {static_cast<int>(extent.width), static_cast<int>(extent.height)};
	}

	bool any_panel = false;
	for (toast::Panel* panel : m_panels) {
		if (panel->enabled() && panel->participatesIn(toast::NodeOwnerParticipation::render) && panel->rmlContext() != nullptr) {
			any_panel = true;
		}
	}
	for (toast::Panel3D* panel : m_world_panels) {
		if (panel->enabled() && panel->participatesIn(toast::NodeOwnerParticipation::render) && panel->rmlContext() != nullptr) {
			any_panel = true;
		}
	}
	if (!any_panel) {
		return;
	}

	frame.ui_slot_guard = m_render_interface->OnFrameBegin(static_cast<uint32_t>(m_frame_counter++));

	auto record_context = [&](Rml::Context* context) -> VkImageView {
		ZoneScopedN("UI record context");
		const Rml::Vector2i dims = context->GetDimensions();
		if (dims.x <= 0 || dims.y <= 0) {
			return nullptr;
		}
		if (!m_render_interface->BeginRecording({static_cast<uint32_t>(dims.x), static_cast<uint32_t>(dims.y)})) {
			return nullptr;
		}
		context->Render();
		if (VkCommandBuffer command = m_render_interface->EndRecording()) {
			frame.ui_command_buffers.push_back(command);
		} else {
			return nullptr;
		}
		return m_render_interface->GetLastOutputView();
	};

	// Panels are in registration order
	for (toast::Panel* panel : m_panels) {
		Rml::Context* context = panel->rmlContext();
		if (!panel->enabled() || !panel->participatesIn(toast::NodeOwnerParticipation::render) || context == nullptr) {
			continue;
		}

		if (viewport_dims.x > 0 && viewport_dims.y > 0 && context->GetDimensions() != viewport_dims) {
			context->SetDimensions(viewport_dims);
		}

		if (const VkImageView view = record_context(context)) {
			frame.ui_output_views.emplace_back(view);
		}
	}

	// World panels render to their own textures, drawn by the world UI pass
	for (toast::Panel3D* panel : m_world_panels) {
		Rml::Context* context = panel->rmlContext();
		if (!panel->enabled() || !panel->participatesIn(toast::NodeOwnerParticipation::render) || context == nullptr) {
			continue;
		}
		panel->syncContextDimensions();

		if (const VkImageView view = record_context(context)) {
			frame.ui_world_panels.push_back({.view = vk::ImageView(view), .model = panel->worldTransformForRender()});
		}
	}
}

auto UISystem::get() noexcept -> UISystem& {
	TOAST_ASSERT(instance, "UI", "UISystem doesn't exist");
	return *instance;
}

auto UISystem::exists() noexcept -> bool {
	return instance != nullptr;
}

void UISystem::tick() noexcept {
	ZoneScoped;
	if (m_documents_reload_pending) {
		m_documents_reload_pending = false;
		if (m_stylesheet_cache_dirty) {
			// RmlUi caches parsed external stylesheets by URI. Rebuilding a document
			// alone would otherwise link the old parsed RCSS again.
			Rml::Factory::ClearStyleSheetCache();
			m_stylesheet_cache_dirty = false;
		}
		reloadAllDocuments();
	}

	/*
	 * TODO: We need to call render here i believe
	 *
	 * [ TICK THREAD ]
	 * 1. Call context->Update() to let RmlUi resolve layout, flexbox, and animations
	 * 2. Call context->Render() -> Customize RenderInterface to DEEP COPY vertices,
	 *    indices, scissors, and texture IDs into a thread-safe staging buffer
	 * 3. Pack all copied geometry into a UI Render proxy
	 * 4. Push the proxy to the Vulkan render thread
	 *
	 * [ VULKAN RENDER THREAD ]
	 * 1. Receive the proxy generated by the logic thread
	 * 2. If geometry changed, update the corresponding Vulkan staging and device host-visible buffers
	 * 3. Record Vulkan commands
	 * 4. Submit the command buffer to the graphics queue
	 */

	UIBinds::flushAllDirty();
	for (int i = 0; i < Rml::GetNumContexts(); i++) {
		Rml::Context* context = Rml::GetContext(i);
		toast::Node* owner = ownerForContext(context);
		if (owner == nullptr || owner->participatesIn(toast::NodeOwnerParticipation::render)) {
			context->Update();
		}
	}
}

auto UISystem::createContext(std::string_view name, glm::ivec2 dimensions, std::optional<float> fixed_dp_ratio) -> Rml::Context* {
	// RmlUi requires globally unique context names
	const auto unique_name = std::format("{}#{}", name, m_next_context_id++);
	Rml::Context* context = Rml::CreateContext(unique_name, {dimensions.x, dimensions.y}, m_render_interface.get());
	if (!context) {
		TOAST_ERROR("UI", "Failed to create context '{}'", unique_name);
		return nullptr;
	}

	context->SetDensityIndependentPixelRatio(fixed_dp_ratio.value_or(m_dp_ratio));
	if (!fixed_dp_ratio.has_value()) {
		m_display_density_contexts.emplace(context);
	}

	TOAST_TRACE("UI", "Created context '{}' ({}x{})", unique_name, dimensions.x, dimensions.y);
	return context;
}

void UISystem::applyDpRatio(float ratio) {
	m_dp_ratio = ratio;
	for (Rml::Context* context : m_display_density_contexts) {
		context->SetDensityIndependentPixelRatio(ratio);
	}
}

void UISystem::setSDLWindow(void* window) {
	if (m_system_interface) {
		m_system_interface->setWindow(static_cast<SDL_Window*>(window));
	}
}

void UISystem::destroyContext(Rml::Context* context) {
	if (!context) {
		return;
	}
	m_display_density_contexts.erase(context);
	Rml::RemoveContext(context->GetName());
}

auto UISystem::loadFontFace(std::string_view uri, bool fallback) -> bool {
	ZoneScoped;

	if (!Rml::LoadFontFace(Rml::String(uri), fallback)) {
		TOAST_WARN("UI", "Failed to load font face '{}'", uri);
		return false;
	}

	TOAST_TRACE("UI", "Loaded font face '{}'", uri);
	return true;
}

auto UISystem::loadFontFace(const assets::Handle<assets::Font>& font, bool fallback) -> bool {
	if (font.path().empty()) {
		return false;
	}
	// The path reads through the assets manager
	return loadFontFace(font.path(), fallback);
}

void UISystem::registerGlobalStyle(const assets::Handle<assets::UIStyle>& style) {
	const uint64_t uid = style.uid().data();
	if (m_global_style_refs[uid]++ == 0) {
		m_global_styles.push_back(style);
	}
}

void UISystem::unregisterGlobalStyle(const assets::Handle<assets::UIStyle>& style) {
	const uint64_t uid = style.uid().data();
	const auto it = m_global_style_refs.find(uid);
	if (it != m_global_style_refs.end() && --it->second == 0) {
		m_global_style_refs.erase(it);
		std::erase(m_global_styles, style);
	}
}

auto UISystem::globalStyleUris() const -> std::vector<std::string> {
	std::vector<std::string> uris;
	uris.reserve(m_global_styles.size());
	for (const auto& style : m_global_styles) {
		if (!style.path().empty()) {
			uris.emplace_back(style.path());
		}
	}
	return uris;
}

void UISystem::registerGlobalScheme(const assets::Handle<assets::ColorScheme>& scheme) {
	const uint64_t uid = scheme.uid().data();
	if (m_global_scheme_refs[uid]++ == 0) {
		m_global_schemes.push_back(scheme);
	}
}

void UISystem::unregisterGlobalScheme(const assets::Handle<assets::ColorScheme>& scheme) {
	const uint64_t uid = scheme.uid().data();
	const auto it = m_global_scheme_refs.find(uid);
	if (it != m_global_scheme_refs.end() && --it->second == 0) {
		m_global_scheme_refs.erase(it);
		std::erase(m_global_schemes, scheme);
	}
}

auto UISystem::colorFromSchemes(std::string_view name) const -> std::optional<glm::vec4> {
	for (const auto& scheme : m_global_schemes) {
		if (scheme.hasValue()) {
			if (auto color = scheme->color(name)) {
				return color;
			}
		}
	}

	// Last resort: the project settings scheme
	if (toast::ProjectSettings::get() != nullptr) {
		auto project_scheme = toast::ProjectSettings::uiSettings().colorScheme();
		if (!project_scheme.hasValue() && !project_scheme.path().empty()) {
			project_scheme = assets::load<assets::ColorScheme>(project_scheme.path());
		}
		if (project_scheme.hasValue()) {
			if (auto color = project_scheme->color(name)) {
				return color;
			}
		}
	}

	return std::nullopt;
}

auto UISystem::colorHex(std::string_view name) const -> std::optional<std::string> {
	const assets::ColorScheme* scope_scheme = m_localization_stack.empty() ? nullptr : m_localization_stack.back().color_scheme;
	if (scope_scheme != nullptr) {
		if (auto hex = scope_scheme->hex(name)) {
			return hex;
		}
	}
	if (auto color = colorFromSchemes(name)) {
		return assets::ColorScheme::toHex(*color);
	}
	return std::nullopt;
}

void UISystem::pushLocalizationScope(LocalizationScope scope) {
	m_localization_stack.push_back(std::move(scope));
}

void UISystem::popLocalizationScope() {
	if (!m_localization_stack.empty()) {
		m_localization_stack.pop_back();
	}
}

void UISystem::registerGlobalLocalization(const assets::Handle<assets::Localization>& loc) {
	const uint64_t uid = loc.uid().data();
	if (m_global_localization_refs[uid]++ == 0) {
		m_global_localizations.push_back(loc);
	}
}

void UISystem::unregisterGlobalLocalization(const assets::Handle<assets::Localization>& loc) {
	const uint64_t uid = loc.uid().data();
	const auto it = m_global_localization_refs.find(uid);
	if (it != m_global_localization_refs.end() && --it->second == 0) {
		m_global_localization_refs.erase(it);
		std::erase(m_global_localizations, loc);
	}
}

void UISystem::registerGlobalImageLocalization(const assets::Handle<assets::ImageLocalization>& loc) {
	const uint64_t uid = loc.uid().data();
	if (m_global_image_localization_refs[uid]++ == 0) {
		m_global_image_localizations.push_back(loc);
	}
}

void UISystem::unregisterGlobalImageLocalization(const assets::Handle<assets::ImageLocalization>& loc) {
	const uint64_t uid = loc.uid().data();
	const auto it = m_global_image_localization_refs.find(uid);
	if (it != m_global_image_localization_refs.end() && --it->second == 0) {
		m_global_image_localization_refs.erase(it);
		std::erase(m_global_image_localizations, loc);
	}
}

auto UISystem::currentLanguage() const -> std::string {
	if (!m_language.empty()) {
		return m_language;
	}
	// Default to the first project language on first use
	if (toast::ProjectSettings::get() != nullptr) {
		const auto& languages = toast::ProjectSettings::uiSettings().languages();
		if (!languages.empty()) {
			return languages.front();
		}
	}
	return "en";
}

void UISystem::setLanguage(std::string language) {
	const auto* settings = toast::ProjectSettings::get();
	if (settings != nullptr) {
		const auto& languages = toast::ProjectSettings::uiSettings().languages();
		if (!languages.empty() && std::ranges::find(languages, language) == languages.end()) {
			std::string configured;
			for (const auto& item : languages) {
				if (!configured.empty()) {
					configured += ", ";
				}
				configured += item;
			}
			TOAST_WARN("UI", "Ignoring unknown UI language '{}'; configured languages are [{}]", language, configured);
			return;
		}
	}
	if (language == currentLanguage()) {
		return;
	}
	m_language = std::move(language);
	TOAST_INFO("UI", "Switching UI language to '{}'", m_language);

	// Reloading re-runs TranslateString and re-applies localized images with the new language
	reloadAllDocuments();
}

void UISystem::reloadAllDocuments() {
	for (toast::Panel* panel : m_panels) {
		panel->reloadDocument();
	}
	for (toast::Panel3D* panel : m_world_panels) {
		panel->reloadDocument();
	}
}

auto UISystem::translate(std::string_view input) const -> std::optional<std::string> {
	TextFormatContext format_ctx;
	format_ctx.color_resolver = [this](std::string_view name) { return colorHex(name); };

	auto trim = [](std::string_view value) {
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
			value.remove_prefix(1);
		}
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
			value.remove_suffix(1);
		}
		return value;
	};

	const std::string_view id = trim(input);
	if (id.empty()) {
		return std::nullopt;
	}
	if (auto localized = lookupLocalizationRaw(id)) {
		return formatText(*localized, format_ctx);
	}

	// Formatted ids are resolved before span markup is injected
	const bool has_format = id.find("${") != std::string_view::npos || id.find("$b") != std::string_view::npos ||
	                        id.find("$i") != std::string_view::npos || id.find("$c") != std::string_view::npos ||
	                        id.find("$$") != std::string_view::npos;
	if (!has_format) {
		return std::nullopt;
	}

	std::string resolved;
	resolved.reserve(id.size() + 16);
	auto append_text = [&](std::string_view segment) {
		const std::string_view key = trim(segment);
		if (key.empty()) {
			resolved.append(segment);
			return;
		}
		const size_t leading = static_cast<size_t>(key.data() - segment.data());
		resolved.append(segment.substr(0, leading));
		if (auto localized = lookupLocalizationRaw(key)) {
			resolved.append(*localized);
		} else {
			resolved.append(key);
		}
		resolved.append(segment.substr(leading + key.size()));
	};

	size_t cursor = 0;
	while (cursor < id.size()) {
		const size_t marker = id.find('$', cursor);
		if (marker == std::string_view::npos) {
			append_text(id.substr(cursor));
			break;
		}
		append_text(id.substr(cursor, marker - cursor));
		if (marker + 1 >= id.size()) {
			resolved.push_back('$');
			break;
		}
		if (id[marker + 1] == '{') {
			const size_t end = id.find('}', marker + 2);
			if (end == std::string_view::npos) {
				resolved.append(id.substr(marker));
				break;
			}
			resolved.append(id.substr(marker, end - marker + 1));
			cursor = end + 1;
		} else {
			resolved.append(id.substr(marker, 2));
			cursor = marker + 2;
		}
	}
	return formatText(resolved, format_ctx);
}

auto UISystem::lookupLocalizationRaw(std::string_view id) const -> std::optional<std::string> {
	const std::string language = currentLanguage();
	auto lookup = [&](const assets::Localization* loc) -> std::optional<std::string> {
		if (loc != nullptr && loc->has(id)) {
			return loc->text(id, language);
		}
		return std::nullopt;
	};
	if (!m_localization_stack.empty()) {
		for (const assets::Localization* loc : m_localization_stack.back().texts) {
			if (auto localized = lookup(loc)) {
				return localized;
			}
		}
	}
	for (const auto& loc : m_global_localizations) {
		if (auto localized = lookup(loc.hasValue() ? loc.operator->() : nullptr)) {
			return localized;
		}
	}
	return std::nullopt;
}

auto UISystem::localizedImage(std::string_view id) const -> std::string {
	const std::string language = currentLanguage();

	auto lookup = [&](const assets::ImageLocalization* loc) -> std::string {
		if (loc != nullptr && loc->has(id)) {
			return loc->image(id, language);
		}
		return {};
	};

	if (!m_localization_stack.empty()) {
		for (const assets::ImageLocalization* loc : m_localization_stack.back().images) {
			if (std::string ref = lookup(loc); !ref.empty()) {
				if (ref.contains("://")) {
					return ref;
				}
				if (ref.size() == 11) {
					return assets::AssetManager::getURI(toast::UID(toast::UID::fromString(ref)));
				}
				return ref;
			}
		}
	}
	for (const auto& loc : m_global_image_localizations) {
		if (std::string ref = lookup(loc.hasValue() ? loc.operator->() : nullptr); !ref.empty()) {
			if (ref.contains("://")) {
				return ref;
			}
			if (ref.size() == 11) {
				return assets::AssetManager::getURI(toast::UID(toast::UID::fromString(ref)));
			}
			return ref;
		}
	}
	return {};
}

void UISystem::registerPanel(toast::Panel* panel) {
	if (std::ranges::find(m_panels, panel) == m_panels.end()) {
		m_panels.push_back(panel);
	}
}

void UISystem::unregisterPanel(toast::Panel* panel) {
	std::erase(m_panels, panel);
}

void UISystem::registerWorldPanel(toast::Panel3D* panel) {
	if (std::ranges::find(m_world_panels, panel) == m_world_panels.end()) {
		m_world_panels.push_back(panel);
	}
}

void UISystem::unregisterWorldPanel(toast::Panel3D* panel) {
	std::erase(m_world_panels, panel);
}

void UISystem::registerContextOwner(Rml::Context* context, toast::Node* owner) {
	if (context != nullptr && owner != nullptr) {
		m_context_owners[context] = owner;
	}
}

void UISystem::unregisterContextOwner(Rml::Context* context) {
	m_context_owners.erase(context);
}

auto UISystem::ownerForContext(Rml::Context* context) const -> toast::Node* {
	const auto it = m_context_owners.find(context);
	return it != m_context_owners.end() ? it->second : nullptr;
}

}
