/// @file VulkanCore.hpp
/// @author dario
/// @date 14/05/2026.

#pragma once

#include "vulkan_common.hpp"

#include <external/inc/renderdoc/renderdoc_app.h>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace renderer {

/**
 * @brief Represents the suitability score of a Vulkan physical device based on various criteria
 */
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

	/**
	 * @brief Converts the device score to a string representation.
	 * @return A string containing the device score details.
	 */
	[[nodiscard]]
	auto toString() const noexcept -> std::string;
};

/// @brief Manages Vulkan instance, device initialization, and memory allocation
class VulkanCore {
public:
	VulkanCore(
	    std::span<const char* const> required_instance_extensions, std::span<const char* const> required_device_extensions = {}
	) noexcept;
	~VulkanCore() = default;

	// Prevent copying
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

	/// @brief Whether validation layers are enabled
	[[nodiscard]]
	auto validationEnabled() const noexcept -> bool {
		return m_validation_enabled;
	}

	/// @brief Whether the selected device supports anisotropic filtering
	[[nodiscard]]
	auto supportsSamplerAnisotropy() const noexcept -> bool {
		return m_sampler_anisotropy_supported;
	}

	/// @brief Device limit to clamp requested sampler anisotropy against
	[[nodiscard]]
	auto maxSamplerAnisotropy() const noexcept -> float {
		return m_max_sampler_anisotropy;
	}

	// TODO: UI system should submit on the render thread
	/// @brief Guards graphics queue submission; the render thread and the UI system submit on the same queue
	[[nodiscard]]
	auto graphicsSubmitMutex() const noexcept -> std::mutex& {
		return m_graphics_submit_mutex;
	}

private:
	/**
	 * Evaluates available Vulkan physical devices and selects the most suitable one
	 * based on required extension support, queue family constraints, and a calculated suitability score.
	 * Updates internal state with the selected device and corresponding queue family indices
	 *
	 * Logs evaluation details, scoring breakdowns, and rejection criteria for each candidate
	 * @param required_device_extensions A span of C-string pointers specifying the Vulkan device
	 *        extensions that must be supported by the selected physical device
	 */
	void pickPhysicalDevice(std::span<const char* const> required_device_extensions);
	/**
	 * Creates a Vulkan logical device and initializes the associated memory allocator.
	 */
	void createLogicalDeviceAndAllocator(std::span<const char* const> required_device_extensions);

	/**
	 * Evaluates a Vulkan physical device suitability by computing a weighted score based on device type,
	 * available memory, hardware limits, feature support, extension availability, API version, and queue
	 * family configuration. Required extensions are validated against the device, applying penalties for
	 * missing capabilities and rewards for supported optional extensions and modern Vulkan features
	 *
	 * @param device The Vulkan physical device to evaluate.
	 * @param required_device_extensions A span of required device extension names that the device must support
	 * @return A DeviceScore structure containing individual category scores and the aggregated total score
	 */
	[[nodiscard]]
	auto calculateDeviceScore(const vk::PhysicalDevice& device, std::span<const char* const> required_device_extensions)
	    -> DeviceScore;

	[[nodiscard]]
	auto checkValidationLayerSupport() -> bool;

	bool m_validation_enabled = false;

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

	// renderdoc api
	RENDERDOC_API_1_6_0* rdoc_api = nullptr;
};
}
