#include "ui_system.hpp"

#include "nodes/panels.hpp"
#include "render/rmlui_renderer_vk.h"
#include "render/ui_pass.hpp"
#include "ui_file_interface.hpp"
#include "ui_input.hpp"
#include "ui_system_interface.hpp"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cassert>
#include <toast/log.hpp>
#include <toast/project_settings.hpp>
#include <toast/renderer/vulkan_core.hpp>
#include <tracy/Tracy.hpp>

namespace ui {

UISystem::UISystem() noexcept {
	ZoneScoped;
	TOAST_ASSERT(not instance, "UI", "UISystem already exists");
	instance = this;

	m_file_interface = std::make_unique<UIFileInterface>();
	m_system_interface = std::make_unique<UISystemInterface>();

	Rml::SetFileInterface(m_file_interface.get());
	Rml::SetSystemInterface(m_system_interface.get());

	if (!Rml::Initialise()) {
		TOAST_ERROR("UI", "RmlUi failed to initialise");
		return;
	}

	m_input_router = std::make_unique<UIInputRouter>();

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
		if (panel->enabled() && panel->rmlContext() != nullptr) {
			any_panel = true;
		}
	}
	for (toast::Panel3D* panel : m_world_panels) {
		if (panel->enabled() && panel->rmlContext() != nullptr) {
			any_panel = true;
		}
	}
	if (!any_panel) {
		return;
	}

	frame.ui_slot_guard = m_render_interface->OnFrameBegin(static_cast<uint32_t>(m_frame_counter++));

	auto record_context = [&](Rml::Context* context) -> VkImageView {
		const Rml::Vector2i dims = context->GetDimensions();
		if (dims.x <= 0 || dims.y <= 0) {
			return nullptr;
		}
		m_render_interface->BeginRecording({static_cast<uint32_t>(dims.x), static_cast<uint32_t>(dims.y)});
		context->Render();
		frame.ui_command_buffers.push_back(m_render_interface->EndRecording());
		return m_render_interface->GetLastOutputView();
	};

	// Panels are in registration order
	for (toast::Panel* panel : m_panels) {
		Rml::Context* context = panel->rmlContext();
		if (!panel->enabled() || context == nullptr) {
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
		if (!panel->enabled() || context == nullptr) {
			continue;
		}

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

	for (int i = 0; i < Rml::GetNumContexts(); i++) {
		Rml::GetContext(i)->Update();
	}
}

auto UISystem::createContext(std::string_view name, glm::ivec2 dimensions) -> Rml::Context* {
	// RmlUi requires globally unique context names
	const auto unique_name = std::format("{}#{}", name, m_next_context_id++);
	Rml::Context* context = Rml::CreateContext(unique_name, {dimensions.x, dimensions.y}, m_render_interface.get());
	if (!context) {
		TOAST_ERROR("UI", "Failed to create context '{}'", unique_name);
		return nullptr;
	}

	context->SetDensityIndependentPixelRatio(m_dp_ratio);

	TOAST_TRACE("UI", "Created context '{}' ({}x{})", unique_name, dimensions.x, dimensions.y);
	return context;
}

void UISystem::applyDpRatio(float ratio) {
	m_dp_ratio = ratio;
	for (int i = 0; i < Rml::GetNumContexts(); i++) {
		Rml::GetContext(i)->SetDensityIndependentPixelRatio(ratio);
	}
}

void UISystem::destroyContext(Rml::Context* context) {
	if (!context) {
		return;
	}
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

auto UISystem::loadFontFace(const assets::AssetHandle<assets::Font>& font, bool fallback) -> bool {
	if (font.path().empty()) {
		return false;
	}
	// The path reads through the assets manager
	return loadFontFace(font.path(), fallback);
}

void UISystem::loadFontFamily(const assets::AssetHandle<assets::FontFamily>& family) {
	ZoneScoped;

	if (!family.hasValue()) {
		TOAST_WARN("UI", "Font family '{}' is not loaded", family.path());
		return;
	}

	const auto family_name = Rml::String(family->name());
	for (const toast::UID font_uid : family->fonts()) {
		auto font = assets::load<assets::Font>(font_uid);
		if (!font.hasValue()) {
			TOAST_WARN("UI", "Font family '{}' references missing font {}", family->name(), font_uid);
			continue;
		}

		const auto& bytes = font->get();
		const bool loaded = Rml::LoadFontFace(
		    {reinterpret_cast<const Rml::byte*>(bytes.data()), bytes.size()}, family_name, Rml::Style::FontStyle::Normal
		);
		if (!loaded) {
			TOAST_WARN("UI", "Failed to load font {} into family '{}'", font_uid, family->name());
			continue;
		}

		if (std::ranges::find(m_retained_fonts, font) == m_retained_fonts.end()) {
			m_retained_fonts.push_back(std::move(font));
		}
	}

	TOAST_TRACE("UI", "Loaded font family '{}'", family->name());
}

void UISystem::registerGlobalStyle(const assets::AssetHandle<assets::UIStyle>& style) {
	if (std::ranges::find(m_global_styles, style) == m_global_styles.end()) {
		m_global_styles.push_back(style);
	}
}

void UISystem::unregisterGlobalStyle(const assets::AssetHandle<assets::UIStyle>& style) {
	std::erase(m_global_styles, style);
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

void UISystem::registerGlobalScheme(const assets::AssetHandle<assets::ColorScheme>& scheme) {
	if (std::ranges::find(m_global_schemes, scheme) == m_global_schemes.end()) {
		m_global_schemes.push_back(scheme);
	}
}

void UISystem::unregisterGlobalScheme(const assets::AssetHandle<assets::ColorScheme>& scheme) {
	std::erase(m_global_schemes, scheme);
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

}
