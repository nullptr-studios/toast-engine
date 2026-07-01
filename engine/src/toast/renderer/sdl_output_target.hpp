/// @file SDLOutputTarget.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "output_target.hpp"
#include "vulkan_swapchain.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <optional>
#include <vector>

namespace toast::renderer {

class SDLOutputTarget final : public IOutputTarget {
public:
	static auto getRequiredInstanceExtensions() -> std::vector<const char*>;
	static auto getRequiredInstanceExtensions(SDL_Window* window) -> std::vector<const char*>;
	static auto getRequiredDeviceExtensions() -> std::vector<const char*>;
	static auto queryExtent(SDL_Window* window) -> vk::Extent2D;

	SDLOutputTarget(const VulkanCore& core, SDL_Window* window, vk::Extent2D preferred_extent);
	~SDLOutputTarget() override = default;

	SDLOutputTarget(const SDLOutputTarget&) = delete;
	auto operator=(const SDLOutputTarget&) -> SDLOutputTarget& = delete;
	SDLOutputTarget(SDLOutputTarget&&) = delete;
	auto operator=(SDLOutputTarget&&) -> SDLOutputTarget& = delete;

	[[nodiscard]]
	auto getExtent() const -> vk::Extent2D override;
	[[nodiscard]]
	auto getColorFormat() const -> vk::Format override;
	[[nodiscard]]
	auto getImageCount() const -> uint32_t override;
	[[nodiscard]]
	auto getColorImage(uint32_t index) const -> const vk::Image& override;
	[[nodiscard]]
	auto getColorAttachment(uint32_t index) const -> const vk::raii::ImageView& override;
	[[nodiscard]]
	auto acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence)
	    -> vk::ResultValue<uint32_t> override;
	[[nodiscard]]
	auto present(uint32_t image_index, vk::Semaphore render_finished) -> vk::Result override;

	auto recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) -> void override;

	auto recreate(vk::Extent2D extent) -> void override;

	[[nodiscard]]
	auto getWindow() const -> SDL_Window*;

	[[nodiscard]]
	auto getSurface() const -> const vk::raii::SurfaceKHR&;

private:
	static auto createSurface(const VulkanCore& core, SDL_Window* window) -> vk::raii::SurfaceKHR;

	const VulkanCore* m_core = nullptr;
	SDL_Window* m_window = nullptr;
	vk::raii::SurfaceKHR m_surface = nullptr;
	std::optional<VulkanSwapchain> m_swapchain;
};

}
