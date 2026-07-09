/// @file VulkanCore.hpp
/// @author dario
/// @date 14/05/2026.

#pragma once

#include "vulkan_common.hpp"

#include <external/inc/renderdoc/renderdoc_app.h>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace toast::renderer {

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
	std::string toString() const noexcept;
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
		return m_physicalDevice;
	}

	[[nodiscard]]
	auto getAllocator() const noexcept -> const vma::raii::Allocator& {
		return *m_allocator;
	}

	[[nodiscard]]
	auto getGraphicsQueueFamilyIndex() const noexcept -> uint32_t {
		return m_graphicsQueueFamilyIndex;
	}

	[[nodiscard]]
	auto getComputeQueueFamilyIndex() const noexcept -> uint32_t {
		return m_computeQueueFamilyIndex;
	}

	[[nodiscard]]
	auto getTransferQueueFamilyIndex() const noexcept -> uint32_t {
		return m_transferQueueFamilyIndex;
	}

	[[nodiscard]]
	vk::Queue getGraphicsQueue() const noexcept {
		return m_graphicsQueue;
	}

	[[nodiscard]]
	vk::Queue getComputeQueue() const noexcept {
		return m_computeQueue;
	}

	[[nodiscard]]
	vk::Queue getTransferQueue() const noexcept {
		return m_transferQueue;
	}

	[[nodiscard]]
	const RENDERDOC_API_1_6_0* getRenderDocAPI() const noexcept {
		return rdoc_api;
	}

	/// @brief Whether validation layers are enabled
	[[nodiscard]]
	bool validationEnabled() const noexcept {
		return m_validationEnabled;
	}

	/// @brief Whether the selected device supports anisotropic filtering
	[[nodiscard]]
	bool supportsSamplerAnisotropy() const noexcept {
		return m_samplerAnisotropySupported;
	}

	/// @brief Device limit to clamp requested sampler anisotropy against
	[[nodiscard]]
	float maxSamplerAnisotropy() const noexcept {
		return m_maxSamplerAnisotropy;
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
	auto calculateDeviceScore(const vk::PhysicalDevice& device, std::span<const char* const> required_device_extensions)
	    -> DeviceScore;

	bool checkValidationLayerSupport();

	bool m_validationEnabled = false;

	vk::raii::Context m_context;
	vk::raii::Instance m_instance = nullptr;
#ifndef NDEBUG
	vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
#endif

	vk::raii::PhysicalDevice m_physicalDevice = nullptr;
	vk::raii::Device m_device = nullptr;

	std::optional<vma::raii::Allocator> m_allocator;

	uint32_t m_graphicsQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
	uint32_t m_computeQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
	uint32_t m_transferQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
	vk::Queue m_graphicsQueue = nullptr;
	vk::Queue m_computeQueue = nullptr;
	vk::Queue m_transferQueue = nullptr;

	bool m_samplerAnisotropySupported = false;
	float m_maxSamplerAnisotropy = 1.0f;

	// renderdoc api
	RENDERDOC_API_1_6_0* rdoc_api = nullptr;
};
}
