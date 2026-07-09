/// @file VulkanSwapchain.cpp
/// @author dario
/// @date 16/05/2026.

#include "vulkan_swapchain.hpp"

#include "toast/log.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace toast::renderer {
namespace {

auto formatUsageFlags(vk::ImageUsageFlags flags) -> std::string {
	std::string out;
	auto append = [&out](const char* name) {
		if (!out.empty()) {
			out.append(", ");
		}
		out.append(name);
	};

	if (flags & vk::ImageUsageFlagBits::eTransferSrc) {
		append("TransferSrc");
	}
	if (flags & vk::ImageUsageFlagBits::eTransferDst) {
		append("TransferDst");
	}
	if (flags & vk::ImageUsageFlagBits::eSampled) {
		append("Sampled");
	}
	if (flags & vk::ImageUsageFlagBits::eStorage) {
		append("Storage");
	}
	if (flags & vk::ImageUsageFlagBits::eColorAttachment) {
		append("ColorAttachment");
	}
	if (flags & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
		append("DepthStencilAttachment");
	}
	if (flags & vk::ImageUsageFlagBits::eTransientAttachment) {
		append("TransientAttachment");
	}
	if (flags & vk::ImageUsageFlagBits::eInputAttachment) {
		append("InputAttachment");
	}

	if (out.empty()) {
		out = "None";
	}

	return out;
}

auto supportsUsage(vk::ImageUsageFlags supported_flags, vk::ImageUsageFlagBits required_flag) -> bool {
	return (supported_flags & required_flag) != vk::ImageUsageFlags {};
}

auto uniqueQueueFamilies(uint32_t graphics_family, uint32_t present_family) -> std::array<uint32_t, 2> {
	return {graphics_family, present_family};
}
}    // namespace

VulkanSwapchain::VulkanSwapchain(const VulkanCore& core, vk::raii::SurfaceKHR& surface, vk::Extent2D preferred_extent)
    : m_core(&core),
      m_surface(&surface) {
	create(preferred_extent);
}

auto VulkanSwapchain::recreate(vk::Extent2D preferred_extent) -> void {
	create(preferred_extent);
}

auto VulkanSwapchain::create(vk::Extent2D preferred_extent) -> void {
	if (!m_core || !m_surface) {
		TOAST_CRITICAL("VulkanSwapchain", "VulkanSwapchain requires a valid core and surface!");
	}

	const auto capabilities = m_core->getPhysicalDevice().getSurfaceCapabilitiesKHR(*m_surface);
	if (capabilities.maxImageExtent.width == 0 || capabilities.maxImageExtent.height == 0 ||
	    capabilities.maxImageArrayLayers == 0) {
		TOAST_ERROR(
		    "VulkanSwapchain",
		    "Invalid surface capabilities: maxExtent {}x{}, maxArrayLayers {}",
		    capabilities.maxImageExtent.width,
		    capabilities.maxImageExtent.height,
		    capabilities.maxImageArrayLayers
		);
		TOAST_CRITICAL("VulkanSwapchain", "Invalid surface capabilities for swapchain creation!");
	}

	const auto formats = m_core->getPhysicalDevice().getSurfaceFormatsKHR(*m_surface);
	const auto present_modes = m_core->getPhysicalDevice().getSurfacePresentModesKHR(*m_surface);

	const auto chosen_format = selectSurfaceFormat(formats);
	const auto chosen_present_mode = selectPresentMode(present_modes);
	const auto chosen_extent = selectExtent(capabilities, preferred_extent);
	const auto present_queue_family_index = findPresentQueueFamilyIndex();
	const auto image_format = chosen_format.format;

	TOAST_TRACE("VulkanSwapchain", "Surface formats: {}", formats.size());
	TOAST_TRACE("VulkanSwapchain", "Present modes: {}", present_modes.size());
	TOAST_TRACE(
	    "VulkanSwapchain",
	    "Chosen format: {} (colorspace {})",
	    vk::to_string(chosen_format.format),
	    vk::to_string(chosen_format.colorSpace)
	);
	TOAST_TRACE("VulkanSwapchain", "Chosen present mode: {}", vk::to_string(chosen_present_mode));
	TOAST_TRACE(
	    "VulkanSwapchain",
	    "Chosen extent: {}x{} (min {}x{}, max {}x{})",
	    chosen_extent.width,
	    chosen_extent.height,
	    capabilities.minImageExtent.width,
	    capabilities.minImageExtent.height,
	    capabilities.maxImageExtent.width,
	    capabilities.maxImageExtent.height
	);

	uint32_t image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
		image_count = capabilities.maxImageCount;
	}

	TOAST_TRACE("VulkanSwapchain", "Swapchain images: {}", image_count);
	TOAST_TRACE("VulkanSwapchain", "Supported usage flags: {}", formatUsageFlags(capabilities.supportedUsageFlags));

	const auto graphics_family = m_core->getGraphicsQueueFamilyIndex();
	const bool concurrent_sharing = graphics_family != present_queue_family_index;
	const auto queue_family_indices = uniqueQueueFamilies(graphics_family, present_queue_family_index);
	const uint32_t* queue_families = concurrent_sharing ? queue_family_indices.data() : nullptr;
	const uint32_t queue_family_index_count = concurrent_sharing ? 2u : 0u;

	vk::ImageUsageFlags image_usage = vk::ImageUsageFlagBits::eColorAttachment;
	if (!supportsUsage(capabilities.supportedUsageFlags, vk::ImageUsageFlagBits::eColorAttachment)) {
		throw std::runtime_error("Toast Engine Error: Surface does not support color attachment usage!");
	}
	TOAST_TRACE("VulkanSwapchain", "Swapchain image usage: {}", formatUsageFlags(image_usage));

	vk::SwapchainCreateInfoKHR swapchain_ci {};
	swapchain_ci.surface = *m_surface;
	swapchain_ci.minImageCount = image_count;
	swapchain_ci.imageFormat = image_format;
	swapchain_ci.imageColorSpace = chosen_format.colorSpace;
	swapchain_ci.imageExtent = chosen_extent;
	swapchain_ci.imageArrayLayers = 1;
	swapchain_ci.imageUsage = image_usage;
	swapchain_ci.imageSharingMode = concurrent_sharing ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive;
	swapchain_ci.queueFamilyIndexCount = queue_family_index_count;
	swapchain_ci.pQueueFamilyIndices = queue_families;
	swapchain_ci.preTransform = capabilities.currentTransform;
	swapchain_ci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	swapchain_ci.presentMode = chosen_present_mode;
	swapchain_ci.clipped = true;
	if (m_swapchain != nullptr) {
		swapchain_ci.oldSwapchain = *m_swapchain;
	}

	auto new_swapchain = vk::raii::SwapchainKHR(m_core->getDevice(), swapchain_ci);
	auto new_images = new_swapchain.getImages();
	std::vector<vk::raii::ImageView> new_image_views;
	new_image_views.reserve(new_images.size());
	for (const auto image : new_images) {
		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = image;
		view_ci.viewType = vk::ImageViewType::e2D;
		view_ci.format = image_format;
		view_ci.components = vk::ComponentMapping {
		  vk::ComponentSwizzle::eIdentity,
		  vk::ComponentSwizzle::eIdentity,
		  vk::ComponentSwizzle::eIdentity,
		  vk::ComponentSwizzle::eIdentity
		};
		view_ci.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

		new_image_views.emplace_back(m_core->getDevice(), view_ci);
	}

	m_image_views.clear();
	m_swapchain = std::move(new_swapchain);
	m_images = std::move(new_images);
	m_present_queue_family_index = present_queue_family_index;
	m_present_queue = m_core->getDevice().getQueue(m_present_queue_family_index, 0);
	m_image_format = image_format;
	m_extent = chosen_extent;
	m_image_views = std::move(new_image_views);
}

auto VulkanSwapchain::findPresentQueueFamilyIndex() const -> uint32_t {
	const auto queue_families = m_core->getPhysicalDevice().getQueueFamilyProperties();
	for (uint32_t index = 0; index < static_cast<uint32_t>(queue_families.size()); ++index) {
		if (m_core->getPhysicalDevice().getSurfaceSupportKHR(index, *m_surface)) {
			return index;
		}
	}

	TOAST_CRITICAL("VulkanSwapchain", "Toast Engine Error: Failed to find a present queue family!");
}

auto VulkanSwapchain::selectSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) const -> vk::SurfaceFormatKHR {
	if (formats.empty()) {
		TOAST_CRITICAL("VulkanSwapchain", "Toast Engine Error: No surface formats are available!");
	}

	const auto preferred = std::find_if(formats.begin(), formats.end(), [](const vk::SurfaceFormatKHR& format) {
		return (format.format == vk::Format::eR8G8B8A8Unorm || format.format == vk::Format::eB8G8R8A8Unorm) &&
		       format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
	});

	return preferred != formats.end() ? *preferred : formats.front();
}

auto VulkanSwapchain::selectPresentMode(const std::vector<vk::PresentModeKHR>& modes) const -> vk::PresentModeKHR {
	// Prefer immediate
	if (std::find(modes.begin(), modes.end(), vk::PresentModeKHR::eImmediate) != modes.end()) {
		return vk::PresentModeKHR::eImmediate;
	}

	// Prefer mailbox triple-buffering next
	if (std::find(modes.begin(), modes.end(), vk::PresentModeKHR::eMailbox) != modes.end()) {
		return vk::PresentModeKHR::eMailbox;
	}

	// Fall back to FIFO vsync
	return vk::PresentModeKHR::eFifo;
}

auto VulkanSwapchain::selectExtent(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D preferred_extent) const
    -> vk::Extent2D {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		return capabilities.currentExtent;
	}

	return {
	  std::clamp(preferred_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
	  std::clamp(preferred_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
	};
}

auto VulkanSwapchain::acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence) const
    -> vk::ResultValue<uint32_t> {
	uint32_t image_index = 0;
	const VkResult result =
	    vkAcquireNextImageKHR(*m_core->getDevice(), *m_swapchain, timeout, image_available, in_flight_fence, &image_index);
	return {static_cast<vk::Result>(result), image_index};
}

auto VulkanSwapchain::present(uint32_t image_index, vk::Semaphore render_finished) const -> vk::Result {
	const VkSwapchainKHR swapchain_handle = static_cast<VkSwapchainKHR>(*m_swapchain);
	const VkSemaphore wait_semaphore = static_cast<VkSemaphore>(render_finished);

	VkPresentInfoKHR present_info {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &wait_semaphore;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &swapchain_handle;
	present_info.pImageIndices = &image_index;

	const VkResult result = vkQueuePresentKHR(static_cast<VkQueue>(m_present_queue), &present_info);
	return static_cast<vk::Result>(result);
}
}
