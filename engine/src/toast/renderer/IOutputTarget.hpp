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
	virtual vk::Extent2D getExtent() const = 0;
	[[nodiscard]]
	virtual vk::Format getColorFormat() const = 0;
	[[nodiscard]]
	virtual uint32_t getImageCount() const = 0;
	[[nodiscard]]
	virtual const vk::Image& getColorImage(uint32_t index) const = 0;
	[[nodiscard]]
	virtual const vk::raii::ImageView& getColorAttachment(uint32_t index) const = 0;

	// OBSOLETE
	[[nodiscard]]
	virtual vk::ResultValue<uint32_t> acquireNextImage(uint64_t timeout, vk::Semaphore imageAvailable, vk::Fence inFlightFence) = 0;
	[[nodiscard]]
	virtual vk::Result present(uint32_t imageIndex, vk::Semaphore renderFinished) = 0;

	virtual void recreate(vk::Extent2D extent) = 0;
};

}
