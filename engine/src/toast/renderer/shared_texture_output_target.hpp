/// @file SharedTextureOutputTarget.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "output_target_base.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

namespace renderer {

class VulkanCore;

/// @brief Description of the latest finished frame
struct ViewportFrameDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t row_pitch = 0;    ///< Bytes per row (= width * 4 for BGRA8)
	uint64_t frame_id = 0;
};

class SharedTextureOutputTarget final : public IOutputTarget {
public:
	SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferred_extent, uint32_t image_count = 3);
	~SharedTextureOutputTarget() override = default;

	SharedTextureOutputTarget(const SharedTextureOutputTarget&) = delete;
	auto operator=(const SharedTextureOutputTarget&) -> SharedTextureOutputTarget& = delete;
	SharedTextureOutputTarget(SharedTextureOutputTarget&&) = delete;
	auto operator=(SharedTextureOutputTarget&&) -> SharedTextureOutputTarget& = delete;

	[[nodiscard]]
	auto getExtent() const -> vk::Extent2D override {
		return m_extent;
	}

	[[nodiscard]]
	auto getColorFormat() const -> vk::Format override {
		return m_color_format;
	}

	[[nodiscard]]
	auto getImageCount() const -> uint32_t override {
		return static_cast<uint32_t>(m_images.size());
	}

	[[nodiscard]]
	auto getColorImage(uint32_t index) const -> const vk::Image& override;
	[[nodiscard]]
	auto getColorAttachment(uint32_t index) const -> const vk::raii::ImageView& override;
	[[nodiscard]]
	auto acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence)
	    -> vk::ResultValue<uint32_t> override;
	[[nodiscard]]
	auto present(uint32_t image_index, vk::Semaphore render_finished) -> vk::Result override;

	// no swapchain semaphore handshake on offscreen
	[[nodiscard]]
	auto usesAcquirePresentSemaphores() const -> bool override {
		return false;
	}

	void recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) override;
	void onImageRenderComplete(uint32_t image_index) override;

	void recreate(vk::Extent2D extent) override;

	/// @brief Copies the latest finished frame's BGRA8 pixels into @p dst
	/// @return 1 if copied; 0 if no frame is available yet; -1 if @p dst_capacity is too small
	[[nodiscard]]
	auto copyLatestFrame(void* dst, uint32_t dst_capacity, ViewportFrameDesc* out) -> int;

private:
	struct SharedImage {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		std::optional<vma::raii::Buffer> staging;
		void* mapped = nullptr;
	};

	void allocateResources(vk::Extent2D extent);

	[[nodiscard]]
	auto imageByteSize() const -> vk::DeviceSize {
		return static_cast<vk::DeviceSize>(m_extent.width) * m_extent.height * 4;
	}

	const VulkanCore* m_core = nullptr;
	vk::Extent2D m_extent;
	vk::Format m_color_format = vk::Format::eB8G8R8A8Unorm;

	std::mutex m_frame_mutex;
	std::vector<SharedImage> m_images;
	uint32_t m_next_acquire_index = 0;

	std::atomic<int32_t> m_latest_ready {-1};
	std::atomic<uint64_t> m_frame_counter {0};
};

}
