/// @file IOutputTarget.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "vulkan_common.hpp"

#include <cstdint>

namespace toast::renderer {

class IOutputTarget {
public:
	virtual ~IOutputTarget() = default;

	/**
	 * @return The extent of the output target (width and height)
	 */
	[[nodiscard]]
	virtual vk::Extent2D getExtent() const = 0;

	/**
	 * @return The color format of the output target
	 */
	[[nodiscard]]
	virtual vk::Format getColorFormat() const = 0;

	/**
	 * @return The number of images in the output target (e.g., swapchain image count)
	 */
	[[nodiscard]]
	virtual uint32_t getImageCount() const = 0;

	/**
	 *
	 * @param index The index of the image to retrieve
	 * @return The color image at the specified index
	 */
	[[nodiscard]]
	virtual const vk::Image& getColorImage(uint32_t index) const = 0;

	/**
	 * @param index The index of the image view to retrieve
	 * @return The color attachment at the specified index
	 */
	[[nodiscard]]
	virtual const vk::raii::ImageView& getColorAttachment(uint32_t index) const = 0;

	/// @brief Acquires the next image to render into (a real swapchain acquire, or a no-op index rotation for
	/// off-screen targets - see usesAcquirePresentSemaphores())
	virtual vk::ResultValue<uint32_t>
	    acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence) = 0;

	/// @brief Presents @p image_index (a real vkQueuePresentKHR for on-screen targets; a no-op for off-screen
	/// targets, which publish their finished frame via onImageRenderComplete() instead)
	virtual vk::Result present(uint32_t image_index, vk::Semaphore render_finished) = 0;

	[[nodiscard]]
	virtual bool usesAcquirePresentSemaphores() const {
		return true;
	}

	/// @brief Records the post-render finalization for @p image_index
	///
	/// The image is in `eColorAttachmentOptimal` when this is called
	/// Implementations transition it to the layout the target needs next
	virtual void recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) = 0;

	/// Called by the renderer once the GPU work that rendered @p image_index has fully completed
	/// Off-screen targets use this to publish the finished frame to consumers
	virtual void onImageRenderComplete(uint32_t image_index) { (void)image_index; }

	/**
	 * @param extent The new extent to recreate the output target with
	 */
	virtual void recreate(vk::Extent2D extent) = 0;
};

}    // namespace toast::renderer
