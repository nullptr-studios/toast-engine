/**
 * @file ui_system.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Singleton owning the RmlUi library
 */

#pragma once
#include "assets.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <toast/renderer/vulkan_renderer.hpp>
#include <vector>

namespace Rml {
class Context;
}

namespace renderer {
class VulkanCore;
}

namespace toast {
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
	auto createContext(std::string_view name, glm::ivec2 dimensions) -> Rml::Context*;
	void destroyContext(Rml::Context* context);

	auto loadFontFace(std::string_view uri, bool fallback = false) -> bool;
	auto loadFontFace(const assets::AssetHandle<assets::Font>& font, bool fallback = false) -> bool;
	void loadFontFamily(const assets::AssetHandle<assets::FontFamily>& family);

	void registerGlobalStyle(const assets::AssetHandle<assets::UIStyle>& style);
	void unregisterGlobalStyle(const assets::AssetHandle<assets::UIStyle>& style);

	[[nodiscard]]
	auto globalStyleUris() const -> std::vector<std::string>;
	void registerGlobalScheme(const assets::AssetHandle<assets::ColorScheme>& scheme);
	void unregisterGlobalScheme(const assets::AssetHandle<assets::ColorScheme>& scheme);

	[[nodiscard]]
	auto colorFromSchemes(std::string_view name) const -> std::optional<glm::vec4>;

	// Panels register themselves on init
	void registerPanel(toast::Panel* panel);
	void unregisterPanel(toast::Panel* panel);
	void registerWorldPanel(toast::Panel3D* panel);
	void unregisterWorldPanel(toast::Panel3D* panel);

	[[nodiscard]]
	auto panels() -> const std::vector<toast::Panel*>& {
		return m_panels;
	}

	[[nodiscard]]
	auto worldPanels() -> const std::vector<toast::Panel3D*>& {
		return m_world_panels;
	}

	void applyDpRatio(float ratio);

	[[nodiscard]]
	auto renderInterface() -> RenderInterface_VK* {
		return m_render_interface.get();
	}

private:
	static inline UISystem* instance = nullptr;

	std::unique_ptr<UIFileInterface> m_file_interface;
	std::unique_ptr<UISystemInterface> m_system_interface;
	std::unique_ptr<RenderInterface_VK> m_render_interface;
	std::unique_ptr<UIInputRouter> m_input_router;

	float m_dp_ratio = 1.0f;

	uint64_t m_next_context_id = 1;    ///< context names must be unique
	uint64_t m_frame_counter = 0;

	std::vector<toast::Panel*> m_panels;
	std::vector<toast::Panel3D*> m_world_panels;

	std::vector<assets::AssetHandle<assets::Font>> m_retained_fonts;
	std::vector<assets::AssetHandle<assets::UIStyle>> m_global_styles;
	std::vector<assets::AssetHandle<assets::ColorScheme>> m_global_schemes;
};

}
