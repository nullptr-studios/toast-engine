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

	[[nodiscard]]
	virtual auto getExtent() const -> vk::Extent2D = 0;
	[[nodiscard]]
	virtual auto getColorFormat() const -> vk::Format = 0;
	[[nodiscard]]
	virtual auto getImageCount() const -> uint32_t = 0;
	[[nodiscard]]
	virtual auto getColorImage(uint32_t index) const -> const vk::Image& = 0;
	[[nodiscard]]
	virtual auto getColorAttachment(uint32_t index) const -> const vk::raii::ImageView& = 0;

	// OBSOLETE
	[[nodiscard, deprecated("Obsolete")]]
	virtual auto acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence)
	    -> vk::ResultValue<uint32_t> = 0;
	[[nodiscard]]
	virtual auto present(uint32_t image_index, vk::Semaphore render_finished) -> vk::Result = 0;

	[[nodiscard]]
	virtual auto usesAcquirePresentSemaphores() const -> bool {
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

	virtual void recreate(vk::Extent2D extent) = 0;
};

}
