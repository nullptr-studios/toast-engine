/// @file SDLOutputTarget.cpp
/// @author dario
/// @date 16/05/2026.

#include "sdl_output_target.hpp"

#include "toast/log.hpp"

#include <stdexcept>

namespace renderer {

auto SDLOutputTarget::getRequiredInstanceExtensions() -> std::vector<const char*> {
	Uint32 count = 0;
	const char* const* raw_extensions = SDL_Vulkan_GetInstanceExtensions(&count);

	TOAST_TRACE("SDLOutputTarget", "SDL required Vulkan instance extensions: {}", count);
	return {raw_extensions, raw_extensions + count};
}

auto SDLOutputTarget::getRequiredInstanceExtensions(SDL_Window* window) -> std::vector<const char*> {
	// SDL3 does not require a window
	(void)window;
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

auto SDLOutputTarget::recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) -> void {
	// Transition the rendered image from color-attachment to present-source for the swapchain
	const vk::ImageMemoryBarrier barrier(
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::AccessFlags {},
	    vk::ImageLayout::eColorAttachmentOptimal,
	    vk::ImageLayout::ePresentSrcKHR,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    m_swapchain->getImage(image_index),
	    vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
	);
	command_buffer.pipelineBarrier(
	    vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, barrier
	);
}

auto SDLOutputTarget::recreate(vk::Extent2D extent) -> void {
	m_swapchain->recreate(extent);
}

}
