/// @file SharedTextureOutputTarget.hpp
/// @author dario
/// @date 16/05/2026

#pragma once

#include "toast/renderer/output/output_target_base.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace renderer {

class VulkanCore;

struct ViewportFrameDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t row_pitch = 0;
	uint64_t frame_id = 0;
};

struct ViewportSharedFrameDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint64_t frame_id = 0;
	uint64_t generation = 0;
	uint64_t memory_size = 0;
	uint32_t slot = 0;
	uint32_t slot_count = 0;
};

struct ViewportSharedHandles {
	intptr_t image = 0;
	intptr_t ready = 0;
	intptr_t done = 0;
};

class SharedTextureOutputTarget final : public IOutputTarget {
public:
	SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferred_extent, uint32_t image_count = 4);
	~SharedTextureOutputTarget() override;

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

	[[nodiscard]]
	auto usesAcquirePresentSemaphores() const -> bool override {
		return false;
	}

	void recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) override;
	void onImageRenderComplete(uint32_t image_index, bool publish = true) override;
	[[nodiscard]]
	auto waitForFreeImage(std::chrono::nanoseconds timeout) -> bool override;
	[[nodiscard]]
	auto submitSync(uint32_t image_index) -> OutputSubmitSync override;

	void recreate(vk::Extent2D extent) override;

	/// @return 1 when copied 0 when no frame is ready -1 when @p dst_capacity is too small
	[[nodiscard]]
	auto copyLatestFrame(void* dst, uint32_t dst_capacity, ViewportFrameDesc* out) -> int;

	/// Reads the next frame back to the CPU even while textures are shared
	void requestReadback();

	/**
	 * @brief Hands frames over as shared textures from the next acquired image on, and stops reading every frame back
	 * @param adapter_luid The compositor's adapter LUID, 0 when it has none (checked on Windows)
	 * @param adapter_uuid The compositor's device UUID, null when it has none (checked elsewhere)
	 * @return false when this device cannot share textures with that adapter
	 */
	auto enableSharedTextures(uint64_t adapter_luid, const uint8_t* adapter_uuid) -> bool;

	void disableSharedTextures();

	[[nodiscard]]
	auto sharedFrame(ViewportSharedFrameDesc* out) -> int;

	[[nodiscard]]
	auto exportSharedSlot(uint64_t generation, uint32_t slot, ViewportSharedHandles* out) -> bool;

	[[nodiscard]]
	auto holdSharedSlot(uint64_t generation, uint32_t slot, uint64_t frame_id) -> bool;

	void releaseSharedSlot(uint64_t generation, uint32_t slot, bool consumed);

private:
	struct SharedImage {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		std::optional<vma::raii::Buffer> staging;
		void* mapped = nullptr;
		bool read_back = false;
		bool shared = false;
		bool in_flight = false;
	};

	struct Interop;

	/// m_frame_mutex held
	[[nodiscard]]
	auto imageBusy(uint32_t index) const -> bool;

	enum class SharingState : uint8_t {
		off,
		requested,
		active,
		failed,
	};

	void allocateResources(vk::Extent2D extent);
	void createInterop();

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
	std::atomic<int32_t> m_latest_readback {-1};
	int32_t m_reading = -1;
	std::condition_variable m_reading_done;
	/// Woken when the editor gives a slot back
	std::condition_variable m_slot_freed;
	std::atomic<uint64_t> m_frame_counter {0};
	std::unique_ptr<Interop> m_interop;
	std::atomic<SharingState> m_sharing {SharingState::off};
	std::atomic<bool> m_readback_requested {false};
};

}
