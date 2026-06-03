/// @file SDLOutputTarget.cpp
/// @author dario
/// @date 16/05/2026.

#include "SDLOutputTarget.hpp"

#include "toast/log.hpp"

#include <stdexcept>

namespace toast::renderer {

auto SDLOutputTarget::getRequiredInstanceExtensions() -> std::vector<const char*> {
	Uint32 count = 0;
	const char* const* raw_extensions = SDL_Vulkan_GetInstanceExtensions(&count);

	TOAST_TRACE("SDLOutputTarget", "SDL required Vulkan instance extensions: {}", count);
	return {raw_extensions, raw_extensions + count};
}

auto SDLOutputTarget::getRequiredInstanceExtensions(SDL_Window* window) -> std::vector<const char*> {
	// SDL3 does not require a window for this query, but keep the overload for compatibility.
	if (!window) {
		return getRequiredInstanceExtensions();
	}
	return getRequiredInstanceExtensions();
}

auto SDLOutputTarget::getRequiredDeviceExtensions() -> std::vector<const char*> {
	return {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
}

auto SDLOutputTarget::queryExtent(SDL_Window* window) -> vk::Extent2D {
	if (!window) {
		TOAST_CRITICAL("SDLOutputTarget", "Toast Engine Error: SDL output target requires a valid window!");
	}

	int width = 0;
	int height = 0;
	SDL_GetWindowSizeInPixels(window, &width, &height);
	if (width <= 0 || height <= 0) {
		width = 1080;
		height = 720;
	}

	return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

auto SDLOutputTarget::createSurface(const VulkanCore& core, SDL_Window* window) -> vk::raii::SurfaceKHR {
	VkSurfaceKHR raw_surface = VK_NULL_HANDLE;
	if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(*core.getInstance()), nullptr, &raw_surface)) {
		TOAST_ERROR("SDLOutputTarget", "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
		TOAST_CRITICAL("SDLOutputTarget", "Toast Engine Error: Failed to create SDL Vulkan surface!");
	}

	TOAST_TRACE("SDLOutputTarget", "SDL Vulkan surface created successfully");
	return {core.getInstance(), raw_surface};
}

SDLOutputTarget::SDLOutputTarget(const VulkanCore& core, SDL_Window* window, vk::Extent2D preferred_extent)
    : m_core(&core),
      m_window(window),
      m_surface(createSurface(core, window)) {
	m_swapchain.emplace(core, m_surface, preferred_extent);
}

auto SDLOutputTarget::getExtent() const -> vk::Extent2D {
	return m_swapchain->getExtent();
}

auto SDLOutputTarget::getColorFormat() const -> vk::Format {
	return m_swapchain->getColorFormat();
}

auto SDLOutputTarget::getImageCount() const -> uint32_t {
	return m_swapchain->getImageCount();
}

auto SDLOutputTarget::getColorImage(uint32_t index) const -> const vk::Image& {
	return m_swapchain->getImage(index);
}

auto SDLOutputTarget::getColorAttachment(uint32_t index) const -> const vk::raii::ImageView& {
	return m_swapchain->getColorAttachment(index);
}

auto SDLOutputTarget::getWindow() const -> SDL_Window* {
	return m_window;
}

auto SDLOutputTarget::getSurface() const -> const vk::raii::SurfaceKHR& {
	return m_surface;
}

auto SDLOutputTarget::acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence)
    -> vk::ResultValue<uint32_t> {
	return m_swapchain->acquireNextImage(timeout, image_available, in_flight_fence);
}

auto SDLOutputTarget::present(uint32_t image_index, vk::Semaphore render_finished) -> vk::Result {
	return m_swapchain->present(image_index, render_finished);
}

auto SDLOutputTarget::recreate(vk::Extent2D extent) -> void {
	m_swapchain->recreate(extent);
}

}
