/// @file VulkanCore.hpp
/// @author dario
/// @date 14/05/2026

#pragma once

#include "vulkan_common.hpp"

#include <algorithm>
#include <external/inc/renderdoc/renderdoc_app.h>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace renderer {

enum class NsightMode : uint8_t {
	none,
	graphics_capture,
	gpu_trace
};

struct DeviceScore {
	int total = 0;
	int device_type = 0;
	int memory = 0;
	int limits = 0;
	int features = 0;
	int extensions = 0;
	int vulkan_support = 0;
	int queue_support = 0;

	int graphics_idx = -1;
	int compute_idx = -1;
	int transfer_idx = -1;

	std::vector<std::string> missing_extensions;

	[[nodiscard]]
	auto toString() const noexcept -> std::string;
};

class VulkanCore {
public:
	VulkanCore(
	    std::span<const char* const> required_instance_extensions, std::span<const char* const> required_device_extensions = {}
	) noexcept;
	~VulkanCore() = default;

	VulkanCore(const VulkanCore&) = delete;
	auto operator=(const VulkanCore&) -> VulkanCore& = delete;

	[[nodiscard]]
	auto getInstance() const noexcept -> const vk::raii::Instance& {
		return m_instance;
	}

	[[nodiscard]]
	auto getDevice() const noexcept -> const vk::raii::Device& {
		return m_device;
	}

	[[nodiscard]]
	auto getPhysicalDevice() const noexcept -> const vk::raii::PhysicalDevice& {
		return m_physical_device;
	}

	[[nodiscard]]
	auto getAllocator() const noexcept -> const vma::raii::Allocator& {
		return *m_allocator;
	}

	[[nodiscard]]
	auto getGraphicsQueueFamilyIndex() const noexcept -> uint32_t {
		return m_graphics_queue_family_index;
	}

	[[nodiscard]]
	auto getComputeQueueFamilyIndex() const noexcept -> uint32_t {
		return m_compute_queue_family_index;
	}

	[[nodiscard]]
	auto getTransferQueueFamilyIndex() const noexcept -> uint32_t {
		return m_transfer_queue_family_index;
	}

	[[nodiscard]]
	auto getGraphicsQueue() const noexcept -> vk::Queue {
		return m_graphics_queue;
	}

	[[nodiscard]]
	auto getComputeQueue() const noexcept -> vk::Queue {
		return m_compute_queue;
	}

	[[nodiscard]]
	auto getTransferQueue() const noexcept -> vk::Queue {
		return m_transfer_queue;
	}

	[[nodiscard]]
	auto getRenderDocAPI() const noexcept -> const RENDERDOC_API_1_6_0* {
		return rdoc_api;
	}

	[[nodiscard]]
	auto isRayTracingSupported() const noexcept -> bool {
		return m_ray_tracing_supported;
	}

	[[nodiscard]]
	auto getBufferAddress(vk::Buffer buffer) const -> vk::DeviceAddress {
		vk::BufferDeviceAddressInfo info {};
		info.buffer = buffer;
		return m_device.getBufferAddress(info);
	}

	[[nodiscard]]
	auto getScratchAllocationSize(vk::DeviceSize needed) const noexcept -> vk::DeviceSize {
		return needed + std::max(m_as_scratch_alignment, 1u);
	}

	/// Pair with getScratchAllocationSize()
	[[nodiscard]]
	auto getAlignedScratchAddress(vk::Buffer buffer) const -> vk::DeviceAddress {
		const auto alignment = static_cast<vk::DeviceAddress>(std::max(m_as_scratch_alignment, 1u));
		return (getBufferAddress(buffer) + alignment - 1) & ~(alignment - 1);
	}

	[[nodiscard]]
	auto isFrameBoundarySupported() const noexcept -> bool {
		return m_frame_boundary_supported;
	}

	[[nodiscard]]
	auto getNsightMode() const noexcept -> NsightMode {
		return m_nsight_mode;
	}

#if defined(_WIN32)
	/// @warning Blocks until the Nsight Graphics host attaches so F12 handler only
	void activateNsightGpuTraceIfNeeded() const;
#endif

	[[nodiscard]]
	auto validationEnabled() const noexcept -> bool {
		return m_validation_enabled;
	}

	[[nodiscard]]
	auto debugUtilsEnabled() const noexcept -> bool {
		return m_debug_utils_enabled;
	}

	[[nodiscard]]
	auto supportsSamplerAnisotropy() const noexcept -> bool {
		return m_sampler_anisotropy_supported;
	}

	[[nodiscard]]
	auto maxSamplerAnisotropy() const noexcept -> float {
		return m_max_sampler_anisotropy;
	}

	// TODO UI system should submit on the render thread
	[[nodiscard]]
	auto graphicsSubmitMutex() const noexcept -> std::mutex& {
		return m_graphics_submit_mutex;
	}

private:
	void pickPhysicalDevice(std::span<const char* const> required_device_extensions);
	void createLogicalDeviceAndAllocator(std::span<const char* const> required_device_extensions);

	[[nodiscard]]
	auto calculateDeviceScore(const vk::PhysicalDevice& device, std::span<const char* const> required_device_extensions)
	    -> DeviceScore;

	[[nodiscard]]
	auto checkValidationLayerSupport() -> bool;

	[[nodiscard]]
	static auto checkInstanceExtensionSupport(std::string_view extension) -> bool;

#if defined(_WIN32)
	void initializeNsightActivity();
#endif

	bool m_validation_enabled = false;
	bool m_debug_utils_enabled = false;

	vk::raii::Context m_context;
	vk::raii::Instance m_instance = nullptr;
#ifndef NDEBUG
	vk::raii::DebugUtilsMessengerEXT m_debug_messenger = nullptr;
#endif

	vk::raii::PhysicalDevice m_physical_device = nullptr;
	vk::raii::Device m_device = nullptr;

	std::optional<vma::raii::Allocator> m_allocator;

	uint32_t m_graphics_queue_family_index = std::numeric_limits<uint32_t>::max();
	uint32_t m_compute_queue_family_index = std::numeric_limits<uint32_t>::max();
	uint32_t m_transfer_queue_family_index = std::numeric_limits<uint32_t>::max();
	vk::Queue m_graphics_queue = nullptr;
	vk::Queue m_compute_queue = nullptr;
	vk::Queue m_transfer_queue = nullptr;

	bool m_sampler_anisotropy_supported = false;
	float m_max_sampler_anisotropy = 1.0f;

	mutable std::mutex m_graphics_submit_mutex;

	RENDERDOC_API_1_6_0* rdoc_api = nullptr;

	mutable NsightMode m_nsight_mode = NsightMode::none;

	bool m_frame_boundary_supported = false;

	bool m_ray_tracing_supported = false;

	uint32_t m_as_scratch_alignment = 0;
	mutable bool m_nsight_gputrace_activated = false;
};
}
