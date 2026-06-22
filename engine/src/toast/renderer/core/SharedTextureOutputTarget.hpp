/// @file SharedTextureOutputTarget.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "IOutputTarget.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace toast::renderer {

class VulkanCore;

/// @brief Description of the latest finished frame
struct ViewportFrameDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t row_pitch = 0;    ///< Bytes per row (= width * 4 for BGRA8)
	uint64_t frame_id = 0;
};

/// @brief Off-screen texture output target for headless rendering or editor viewports
class SharedTextureOutputTarget final : public IOutputTarget {
public:
	SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferredExtent, uint32_t imageCount = 3);
	~SharedTextureOutputTarget() override = default;

	SharedTextureOutputTarget(const SharedTextureOutputTarget&) = delete;
	SharedTextureOutputTarget& operator=(const SharedTextureOutputTarget&) = delete;
	SharedTextureOutputTarget(SharedTextureOutputTarget&&) = delete;
	SharedTextureOutputTarget& operator=(SharedTextureOutputTarget&&) = delete;

	[[nodiscard]]
	vk::Extent2D getExtent() const override {
		return m_extent;
	}

	[[nodiscard]]
	vk::Format getColorFormat() const override {
		return m_colorFormat;
	}

	[[nodiscard]]
	uint32_t getImageCount() const override {
		return static_cast<uint32_t>(m_images.size());
	}

	[[nodiscard]]
	const vk::Image& getColorImage(uint32_t index) const override;
	[[nodiscard]]
	const vk::raii::ImageView& getColorAttachment(uint32_t index) const override;
	[[nodiscard]]
	vk::ResultValue<uint32_t> acquireNextImage(uint64_t timeout, vk::Semaphore image_available, vk::Fence in_flight_fence) override;
	[[nodiscard]]
	vk::Result present(uint32_t image_index, vk::Semaphore render_finished) override;

	// no swapchain semaphore handshake on offscreen
	[[nodiscard]]
	bool usesAcquirePresentSemaphores() const override {
		return false;
	}

	void recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) override;
	void onImageRenderComplete(uint32_t image_index) override;

	void recreate(vk::Extent2D extent) override;

	/// @brief Copies the latest finished frame's BGRA8 pixels into @p dst
	/// @return 1 if copied; 0 if no frame is available yet; -1 if @p dstCapacity is too small
	[[nodiscard]]
	int copyLatestFrame(void* dst, uint32_t dstCapacity, ViewportFrameDesc* out);

private:
	struct SharedImage {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		std::optional<vma::raii::Buffer> staging;
		void* mapped = nullptr;
	};

	void allocateResources(vk::Extent2D extent);

	[[nodiscard]]
	vk::DeviceSize imageByteSize() const {
		return static_cast<vk::DeviceSize>(m_extent.width) * m_extent.height * 4;
	}

	const VulkanCore* m_core = nullptr;
	vk::Extent2D m_extent {};
	vk::Format m_colorFormat = vk::Format::eB8G8R8A8Unorm;

	std::mutex m_frameMutex;
	std::vector<SharedImage> m_images;
	uint32_t m_nextAcquireIndex = 0;

	std::atomic<int32_t> m_latestReady {-1};
	std::atomic<uint64_t> m_frameCounter {0};
};

}
