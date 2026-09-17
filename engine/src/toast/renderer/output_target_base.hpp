/// @file IOutputTarget.hpp
/// @author dario
/// @date 16/05/2026

#pragma once

#include "vulkan_common.hpp"

#include <cstdint>

namespace renderer {

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

	virtual auto acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence)
	    -> vk::ResultValue<uint32_t> = 0;

	virtual auto present(uint32_t image_index, vk::Semaphore render_finished) -> vk::Result = 0;

	[[nodiscard]]
	virtual auto usesAcquirePresentSemaphores() const -> bool {
		return true;
	}

	virtual void recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) = 0;

	virtual void onImageRenderComplete(uint32_t image_index) { (void)image_index; }

	virtual void recreate(vk::Extent2D extent) = 0;

	[[nodiscard]]
	virtual auto isPresentable() const -> bool {
		return true;
	}
};

}    // namespace renderer
