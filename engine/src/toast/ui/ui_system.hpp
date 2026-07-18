/**
 * @file ui_system.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Singleton owning the RmlUi library
 */

#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <toast/renderer/vulkan_renderer.hpp>

namespace Rml {
class Context;
}

namespace renderer {
class VulkanCore;
}

class RenderInterface_VK;

namespace ui {

class UIFileInterface;
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

	[[nodiscard]]
	auto renderInterface() -> RenderInterface_VK* {
		return m_render_interface.get();
	}

private:
	static inline UISystem* instance = nullptr;

	std::unique_ptr<UIFileInterface> m_file_interface;
	std::unique_ptr<UISystemInterface> m_system_interface;
	std::unique_ptr<RenderInterface_VK> m_render_interface;

	uint64_t m_next_context_id = 1;    ///< context names must be unique
	uint64_t m_frame_counter = 0;
};

}
