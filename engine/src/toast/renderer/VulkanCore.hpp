/// @file VulkanCore.hpp
/// @author dario
/// @date 14/05/2026.

#pragma once

#include "vulkan_common.hpp"

#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace toast::renderer {

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
	auto toString() const -> std::string;
};

class VulkanCore {
public:
	/**
	 * Initializes the Vulkan Core instance.
	 * @param parameters The configuration parameters.
	 * @return A reference to the VulkanCore object.
	 */

	VulkanCore(
	    std::span<const char* const> required_instance_extensions, std::span<const char* const> required_device_extensions = {}
	);
	~VulkanCore() = default;

	// Prevent copying
	VulkanCore(const VulkanCore&) = delete;
	auto operator=(const VulkanCore&) -> VulkanCore& = delete;

	[[nodiscard]]
	auto getInstance() const -> const vk::raii::Instance& {
		return m_instance;
	}

	[[nodiscard]]
	auto getDevice() const -> const vk::raii::Device& {
		return m_device;
	}

	[[nodiscard]]
	auto getPhysicalDevice() const -> const vk::raii::PhysicalDevice& {
		return m_physicalDevice;
	}

	[[nodiscard]]
	auto getAllocator() const -> const vma::raii::Allocator& {
		return *m_allocator;
	}

	[[nodiscard]]
	auto getGraphicsQueueFamilyIndex() const -> uint32_t {
		return m_graphicsQueueFamilyIndex;
	}

	[[nodiscard]]
	auto getComputeQueueFamilyIndex() const -> uint32_t {
		return m_computeQueueFamilyIndex;
	}

	[[nodiscard]]
	auto getTransferQueueFamilyIndex() const -> uint32_t {
		return m_transferQueueFamilyIndex;
	}

	[[nodiscard]]
	auto getGraphicsQueue() const -> vk::Queue {
		return m_graphicsQueue;
	}

	[[nodiscard]]
	auto getComputeQueue() const -> vk::Queue {
		return m_computeQueue;
	}

	[[nodiscard]]
	auto getTransferQueue() const -> vk::Queue {
		return m_transferQueue;
	}

private:
	/**
	 * Evaluates available Vulkan physical devices and selects the most suitable one
	 * based on required extension support, queue family constraints, and a calculated suitability score.
	 * Updates internal state with the selected device and corresponding queue family indices.
	 * Logs evaluation details, scoring breakdowns, and rejection criteria for each candidate.
	 *
	 * @param required_device_extensions A span of C-string pointers specifying the Vulkan device
	 *        extensions that must be supported by the selected physical device.
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
	 * missing capabilities and rewards for supported optional extensions and modern Vulkan features.
	 *
	 * @param device The Vulkan physical device to evaluate.
	 * @param required_device_extensions A span of required device extension names that the device must support.
	 * @return A DeviceScore structure containing individual category scores and the aggregated total score.
	 */
	auto calculateDeviceScore(const vk::PhysicalDevice& device, std::span<const char* const> required_device_extensions)
	    -> DeviceScore;

	bool m_validationEnabled = false;

	vk::raii::Context m_context;
	vk::raii::Instance m_instance = nullptr;
#ifndef NDEBUG
	vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
#endif

	vk::raii::PhysicalDevice m_physicalDevice = nullptr;
	vk::raii::Device m_device = nullptr;

	std::optional<vma::raii::Allocator> m_allocator;

	// Use a sentinel invalid value so index 0 is a legitimate family index
	uint32_t m_graphicsQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
	uint32_t m_computeQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
	uint32_t m_transferQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
	vk::Queue m_graphicsQueue = nullptr;
	vk::Queue m_computeQueue = nullptr;
	vk::Queue m_transferQueue = nullptr;
};
}
