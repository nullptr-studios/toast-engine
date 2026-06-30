/// @file VulkanSwapchain.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "vulkan_common.hpp"
#include "vulkan_core.hpp"

#include <vector>

namespace toast::renderer {

/**
 * @class VulkanSwapchain
 * @brief Manages image acquisition and presentation for Vulkan swapchains
 *
 * Handles image format selection, extent management, and present mode selection
 */
class VulkanSwapchain {
public:
	VulkanSwapchain(const VulkanCore& core, vk::raii::SurfaceKHR& surface, vk::Extent2D preferred_extent);
	~VulkanSwapchain() = default;

	// Not Copyable nor movable
	VulkanSwapchain(const VulkanSwapchain&) = delete;
	auto operator=(const VulkanSwapchain&) -> VulkanSwapchain& = delete;
	VulkanSwapchain(VulkanSwapchain&&) = delete;
	auto operator=(VulkanSwapchain&&) -> VulkanSwapchain& = delete;

	void recreate(vk::Extent2D preferred_extent);

	[[nodiscard]]
	auto getExtent() const -> vk::Extent2D {
		return m_extent;
	}

	[[nodiscard]]
	auto getColorFormat() const -> vk::Format {
		return m_image_format;
	}

	[[nodiscard]]
	auto getImageCount() const -> uint32_t {
		return static_cast<uint32_t>(m_images.size());
	}

	[[nodiscard]]
	auto getImage(uint32_t index) const -> const vk::Image& {
		return m_images.at(index);
	}

	[[nodiscard]]
	auto getColorAttachment(uint32_t index) const -> const vk::raii::ImageView& {
		return m_image_views.at(index);
	}

	[[nodiscard]]
	auto acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence) const
	    -> vk::ResultValue<uint32_t>;
	[[nodiscard]]
	auto present(uint32_t image_index, vk::Semaphore render_finished) const -> vk::Result;

private:
	void create(vk::Extent2D preferred_extent);
	[[nodiscard]]
	auto findPresentQueueFamilyIndex() const -> uint32_t;
	[[nodiscard]]
	auto selectSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) const -> vk::SurfaceFormatKHR;
	[[nodiscard]]
	auto selectPresentMode(const std::vector<vk::PresentModeKHR>& modes) const -> vk::PresentModeKHR;
	[[nodiscard]]
	auto selectExtent(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D preferred_extent) const -> vk::Extent2D;

	const VulkanCore* m_core = nullptr;
	vk::raii::SurfaceKHR* m_surface = nullptr;

	vk::raii::SwapchainKHR m_swapchain = nullptr;
	std::vector<vk::Image> m_images;
	std::vector<vk::raii::ImageView> m_image_views;

	vk::Extent2D m_extent;
	vk::Format m_image_format = vk::Format::eUndefined;
	uint32_t m_present_queue_family_index = 0;
};

}
