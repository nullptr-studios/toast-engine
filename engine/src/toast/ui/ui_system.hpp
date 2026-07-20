/**
 * @file ui_system.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Singleton owning the RmlUi library
 */

#pragma once
#include "assets.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <toast/events/listener.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Rml {
class Context;
}

namespace renderer {
class VulkanCore;
}

namespace toast {
class Node;
class Panel;
class Panel3D;
}

class RenderInterface_VK;

namespace ui {

class UIFileInterface;
class UIInputRouter;
class UISystemInterface;

class UISystem {
public:
	UISystem() noexcept;
	~UISystem() noexcept;

	[[nodiscard]]
	static auto get() noexcept -> UISystem&;
	[[nodiscard]]
	static auto exists() noexcept -> bool;

	void tick() noexcept;

	/// @note Must run once the renderer exists
	void initializeRenderer(const renderer::VulkanCore& core);
	void buildDrawFrame(renderer::VulkanRenderer::RenderFrame& frame);

	[[nodiscard]]
	auto createContext(std::string_view name, glm::ivec2 dimensions, std::optional<float> fixed_dp_ratio = std::nullopt)
	    -> Rml::Context*;
	void destroyContext(Rml::Context* context);

	auto loadFontFace(std::string_view uri, bool fallback = false) -> bool;
	auto loadFontFace(const assets::Handle<assets::Font>& font, bool fallback = false) -> bool;

	void registerGlobalStyle(const assets::Handle<assets::UIStyle>& style);
	void unregisterGlobalStyle(const assets::Handle<assets::UIStyle>& style);

	[[nodiscard]]
	auto globalStyleUris() const -> std::vector<std::string>;
	void registerGlobalScheme(const assets::Handle<assets::ColorScheme>& scheme);
	void unregisterGlobalScheme(const assets::Handle<assets::ColorScheme>& scheme);

	[[nodiscard]]
	auto colorFromSchemes(std::string_view name) const -> std::optional<glm::vec4>;
	[[nodiscard]]
	auto colorHex(std::string_view name) const -> std::optional<std::string>;

	struct LocalizationScope {
		std::vector<const assets::Localization*> texts;
		std::vector<const assets::ImageLocalization*> images;
		const assets::ColorScheme* color_scheme = nullptr;
	};

	void pushLocalizationScope(LocalizationScope scope);
	void popLocalizationScope();

	void registerGlobalLocalization(const assets::Handle<assets::Localization>& loc);
	void unregisterGlobalLocalization(const assets::Handle<assets::Localization>& loc);
	void registerGlobalImageLocalization(const assets::Handle<assets::ImageLocalization>& loc);
	void unregisterGlobalImageLocalization(const assets::Handle<assets::ImageLocalization>& loc);

	[[nodiscard]]
	auto language() const -> const std::string& {
		return m_language;
	}

	void setLanguage(std::string language);
	void reloadAllDocuments();

	[[nodiscard]]
	auto translate(std::string_view input) const -> std::optional<std::string>;
	[[nodiscard]]
	auto lookupLocalizationRaw(std::string_view id) const -> std::optional<std::string>;

	[[nodiscard]]
	auto localizedImage(std::string_view id) const -> std::string;

	// Panels register themselves on init
	void registerPanel(toast::Panel* panel);
	void unregisterPanel(toast::Panel* panel);
	void registerWorldPanel(toast::Panel3D* panel);
	void unregisterWorldPanel(toast::Panel3D* panel);
	void registerContextOwner(Rml::Context* context, toast::Node* owner);
	void unregisterContextOwner(Rml::Context* context);

	[[nodiscard]]
	auto ownerForContext(Rml::Context* context) const -> toast::Node*;

	[[nodiscard]]
	auto panels() -> const std::vector<toast::Panel*>& {
		return m_panels;
	}

	[[nodiscard]]
	auto worldPanels() -> const std::vector<toast::Panel3D*>& {
		return m_world_panels;
	}

	void applyDpRatio(float ratio);
	void setSDLWindow(void* window);

	[[nodiscard]]
	auto renderInterface() -> RenderInterface_VK* {
		return m_render_interface.get();
	}

private:
	[[nodiscard]]
	auto currentLanguage() const -> std::string;

	static inline UISystem* instance = nullptr;

	std::unique_ptr<UIFileInterface> m_file_interface;
	std::unique_ptr<UISystemInterface> m_system_interface;
	std::unique_ptr<RenderInterface_VK> m_render_interface;
	std::unique_ptr<UIInputRouter> m_input_router;
	event::Listener m_asset_listener;

	float m_dp_ratio = 1.0f;

	uint64_t m_next_context_id = 1;    ///< context names must be unique
	uint64_t m_frame_counter = 0;

	std::vector<toast::Panel*> m_panels;
	std::vector<toast::Panel3D*> m_world_panels;
	std::unordered_map<Rml::Context*, toast::Node*> m_context_owners;
	std::unordered_set<Rml::Context*> m_display_density_contexts;

	std::vector<assets::Handle<assets::UIStyle>> m_global_styles;
	std::vector<assets::Handle<assets::ColorScheme>> m_global_schemes;
	std::unordered_map<uint64_t, size_t> m_global_style_refs;
	std::unordered_map<uint64_t, size_t> m_global_scheme_refs;

	std::string m_language;
	std::vector<assets::Handle<assets::Localization>> m_global_localizations;
	std::vector<assets::Handle<assets::ImageLocalization>> m_global_image_localizations;
	std::unordered_map<uint64_t, size_t> m_global_localization_refs;
	std::unordered_map<uint64_t, size_t> m_global_image_localization_refs;
	std::vector<LocalizationScope> m_localization_stack;

	bool m_documents_reload_pending = false;
	bool m_stylesheet_cache_dirty = false;
};

}
