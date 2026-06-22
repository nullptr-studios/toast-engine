/// @file VulkanCore.cpp
/// @author dario
/// @date 14/05/2026.

#include "vulkan_core.hpp"

#include "toast/log.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace toast::renderer {

namespace {
constexpr std::size_t k_gigabyte_bytes = 1024ull * 1024ull * 1024ull;
constexpr uint32_t k_invalid_queue_family = std::numeric_limits<uint32_t>::max();

struct QueueFamilySelection {
	uint32_t graphics = k_invalid_queue_family;
	uint32_t compute = k_invalid_queue_family;
	uint32_t transfer = k_invalid_queue_family;

	[[nodiscard]]
	auto isComplete() const -> bool {
		return graphics != k_invalid_queue_family && compute != k_invalid_queue_family && transfer != k_invalid_queue_family;
	}

	[[nodiscard]]
	auto isDistinct() const -> bool {
		return graphics != compute && graphics != transfer && compute != transfer;
	}
};

auto queueFamilyFlagsToString(vk::QueueFlags flags) -> std::string {
	std::string result;
	if (flags & vk::QueueFlagBits::eGraphics) {
		result += " Graphics";
	}
	if (flags & vk::QueueFlagBits::eCompute) {
		result += " Compute";
	}
	if (flags & vk::QueueFlagBits::eTransfer) {
		result += " Transfer";
	}
	return result;
}

auto debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data
) -> VkBool32 {
	(void)p_user_data;
	(void)message_type;

	const char* message_type_string = "Unknown";
	switch (message_severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: message_type_string = "Verbose"; break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: message_type_string = "Info"; break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: message_type_string = "Warning"; break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: message_type_string = "Error"; break;
		default: break;
	}

	const char* message = (p_callback_data != nullptr && p_callback_data->pMessage != nullptr) ? p_callback_data->pMessage : "";
	switch (message_severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			TOAST_TRACE("VulkanValidationLayers", "[Vulkan Validation] [{}]: {}", message_type_string, message);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			TOAST_INFO("VulkanValidationLayers", "[Vulkan Validation] [{}]: {}", message_type_string, message);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			TOAST_WARN("VulkanValidationLayers", "[Vulkan Validation] [{}]: {}", message_type_string, message);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			TOAST_ERROR("VulkanValidationLayers", "[Vulkan Validation] [{}]: {}", message_type_string, message);
			break;
		default: break;
	}

	return VK_FALSE;
}

auto versionToString(uint32_t version) -> std::string {
	return std::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
}

auto deviceTypeToString(vk::PhysicalDeviceType type) -> std::string {
	switch (type) {
		case vk::PhysicalDeviceType::eDiscreteGpu: return "Discrete";
		case vk::PhysicalDeviceType::eIntegratedGpu: return "Integrated";
		case vk::PhysicalDeviceType::eVirtualGpu: return "Virtual";
		case vk::PhysicalDeviceType::eCpu: return "CPU";
		default: return "Other";
	}
}

auto joinExtensions(const std::vector<vk::ExtensionProperties>& extensions) -> std::string {
	std::string out;
	for (size_t i = 0; i < extensions.size(); ++i) {
		if (i > 0) {
			out.append("\n");
		}
		out.append(extensions[i].extensionName.data());
	}
	return out;
}

auto formatQueueFlags(vk::QueueFlags flags) -> std::string {
	return queueFamilyFlagsToString(flags);
}

auto selectQueueFamilies(const vk::PhysicalDevice& device) -> QueueFamilySelection {
	QueueFamilySelection selection {};
	const auto queue_families = device.getQueueFamilyProperties();

	for (uint32_t i = 0; i < queue_families.size(); ++i) {
		if ((queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags {}) {
			selection.graphics = i;
			break;
		}
	}

	for (uint32_t i = 0; i < queue_families.size(); ++i) {
		const auto flags = queue_families[i].queueFlags;
		if ((flags & vk::QueueFlagBits::eCompute) == vk::QueueFlags {}) {
			continue;
		}
		if ((flags & vk::QueueFlagBits::eGraphics) == vk::QueueFlags {} && i != selection.graphics) {
			selection.compute = i;
			break;
		}
	}

	if (selection.compute == k_invalid_queue_family) {
		for (uint32_t i = 0; i < queue_families.size(); ++i) {
			const auto flags = queue_families[i].queueFlags;
			if ((flags & vk::QueueFlagBits::eCompute) != vk::QueueFlags {} && i != selection.graphics) {
				selection.compute = i;
				break;
			}
		}
	}

	for (uint32_t i = 0; i < queue_families.size(); ++i) {
		const auto flags = queue_families[i].queueFlags;
		if ((flags & vk::QueueFlagBits::eTransfer) == vk::QueueFlags {}) {
			continue;
		}
		if ((flags & vk::QueueFlagBits::eGraphics) == vk::QueueFlags {} &&
		    (flags & vk::QueueFlagBits::eCompute) == vk::QueueFlags {} && i != selection.graphics && i != selection.compute) {
			selection.transfer = i;
			break;
		}
	}

	if (selection.transfer == k_invalid_queue_family) {
		for (uint32_t i = 0; i < queue_families.size(); ++i) {
			const auto flags = queue_families[i].queueFlags;
			if ((flags & vk::QueueFlagBits::eTransfer) != vk::QueueFlags {} && i != selection.graphics && i != selection.compute) {
				selection.transfer = i;
				break;
			}
		}
	}

	if (selection.transfer == k_invalid_queue_family) {
		for (uint32_t i = 0; i < queue_families.size(); ++i) {
			const auto flags = queue_families[i].queueFlags;
			if ((flags & vk::QueueFlagBits::eTransfer) != vk::QueueFlags {} && i != selection.graphics) {
				selection.transfer = i;
				break;
			}
		}
	}

	if (selection.transfer == k_invalid_queue_family) {
		for (uint32_t i = 0; i < queue_families.size(); ++i) {
			if ((queue_families[i].queueFlags & vk::QueueFlagBits::eTransfer) != vk::QueueFlags {}) {
				selection.transfer = i;
				break;
			}
		}
	}

	return selection;
}

auto joinRequiredExtensions(std::span<const char* const> extensions) -> std::string {
	std::string out;
	for (size_t i = 0; i < extensions.size(); ++i) {
		if (i > 0) {
			out.append(", ");
		}
		out.append(extensions[i]);
	}
	return out;
}

auto deviceNameString(const vk::PhysicalDeviceProperties& props) -> std::string {
	return {props.deviceName.data()};
}
}    // namespace

auto DeviceScore::toString() const -> std::string {
	std::string str = "Device Score Breakdown:\n";
	str += "  Device type: " + std::to_string(device_type) + "\n";
	str += "  Memory: " + std::to_string(memory) + "\n";
	str += "  Limits: " + std::to_string(limits) + "\n";
	str += "  Features: " + std::to_string(features) + "\n";
	str += "  Extensions: " + std::to_string(extensions) + "\n";
	str += "  Vulkan support: " + std::to_string(vulkan_support) + "\n";
	str += "  Queue support: " + std::to_string(queue_support) + "\n";
	str += "  Total: " + std::to_string(total);
	return str;
}

VulkanCore::VulkanCore(
    std::span<const char* const> required_instance_extensions, std::span<const char* const> required_device_extensions
) {
#ifdef DEBUG
<<<<<<<< HEAD:engine/src/toast/renderer/core/VulkanCore.cpp
	m_validationEnabled = checkValidationLayerSupport();
#else
	m_validationEnabled = false;
#endif

	TOAST_INFO("VulkanCore", "Validation layers: {}", m_validationEnabled ? "enabled" : "disabled");
========
    : m_validation_enabled(true)
#else
    : m_validation_enabled(false)
#endif
{
	TOAST_INFO("VulkanCore", "Validation layers: {}", m_validation_enabled ? "enabled" : "disabled");
>>>>>>>> origin/dev:engine/src/toast/renderer/vulkan_core.cpp
	TOAST_TRACE("VulkanCore", "Required instance extensions: {}", joinRequiredExtensions(required_instance_extensions));
	TOAST_TRACE("VulkanCore", "Required device extensions: {}", joinRequiredExtensions(required_device_extensions));

	vk::ApplicationInfo app_info("SUPER DUPER TOASTY GAME", 1, "TOAST ENGINE", 1, VK_API_VERSION_1_4);

	std::vector<const char*> layers;
	if (m_validation_enabled) {
		layers.push_back("VK_LAYER_KHRONOS_validation");
	}

	std::vector extensions(required_instance_extensions.begin(), required_instance_extensions.end());
	if (m_validation_enabled) {
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	vk::InstanceCreateInfo instance_ci({}, &app_info, layers, extensions);
	m_instance = vk::raii::Instance(m_context, instance_ci);

	if (m_validation_enabled) {
		vk::DebugUtilsMessengerCreateInfoEXT debug_ci {};
		debug_ci.messageSeverity =
		    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
		debug_ci.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
		                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
		debug_ci.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback);
#ifndef NDEBUG
		m_debug_messenger = vk::raii::DebugUtilsMessengerEXT(m_instance, debug_ci);
#endif
	}

	pickPhysicalDevice(required_device_extensions);
	createLogicalDeviceAndAllocator(required_device_extensions);
}

void VulkanCore::pickPhysicalDevice(std::span<const char* const> required_device_extensions) {
	vk::raii::PhysicalDevices devices(m_instance);
	if (devices.empty()) {
		TOAST_CRITICAL("VulkanCore", "Toast Engine Error: Failed to find GPUs with Vulkan support!");
	}

	TOAST_TRACE("VulkanCore", "Found {} Vulkan device(s)", devices.size());

	int best_score = -1;
	vk::raii::PhysicalDevice best_device = nullptr;
	QueueFamilySelection best_queue_families {};

	for (const auto& device : devices) {
		const auto props = device.getProperties();
		const auto extensions = device.enumerateDeviceExtensionProperties();
		const auto queues = device.getQueueFamilyProperties();

		DeviceScore device_score = calculateDeviceScore(device, required_device_extensions);
		QueueFamilySelection queue_families {
		  .graphics = device_score.graphics_idx >= 0 ? static_cast<uint32_t>(device_score.graphics_idx) : k_invalid_queue_family,
		  .compute = device_score.compute_idx >= 0 ? static_cast<uint32_t>(device_score.compute_idx) : k_invalid_queue_family,
		  .transfer = device_score.transfer_idx >= 0 ? static_cast<uint32_t>(device_score.transfer_idx) : k_invalid_queue_family,
		};

		// Log device info
		TOAST_TRACE(
		    "VulkanCore",
		    "Device: {} (type: {}, API: {}, driver: {}, vendor: {}, device: {})",
		    deviceNameString(props),
		    deviceTypeToString(props.deviceType),
		    versionToString(props.apiVersion),
		    versionToString(props.driverVersion),
		    props.vendorID,
		    props.deviceID
		);
		TOAST_TRACE("VulkanCore", "  Queue families: {}", queues.size());
		for (std::size_t i = 0; i < queues.size(); ++i) {
			TOAST_TRACE("VulkanCore", "    Queue family {} | flags:{}", i, formatQueueFlags(queues[i].queueFlags));
		}
		TOAST_TRACE("VulkanCore", "  Extensions: {}", joinExtensions(extensions));

		// Log score breakdown
		TOAST_TRACE("VulkanCore", "  {}", device_score.toString());

		if (!device_score.missing_extensions.empty()) {
			std::string missing;
			for (size_t i = 0; i < device_score.missing_extensions.size(); ++i) {
				if (i > 0) {
					missing.append(", ");
				}
				missing.append(device_score.missing_extensions[i]);
			}
			TOAST_WARN("VulkanCore", "  Missing required extensions: {}", missing);
		}

		// Reject device if missing required extensions
		if (!device_score.missing_extensions.empty()) {
			TOAST_WARN("VulkanCore", "  Device rejected: missing required extensions");
			continue;
		}

		if (device_score.total <= 0) {
			TOAST_WARN("VulkanCore", "  Device rejected: total score {} is not valid", device_score.total);
			continue;
		}

		if (!queue_families.isComplete()) {
			TOAST_WARN("VulkanCore", "  Device rejected: required queue type not present");
			continue;
		}

		if (!queue_families.isDistinct()) {
			TOAST_WARN(
			    "VulkanCore", "  Device Waring: graphics, compute and transfer are not using distinct family queues, PERFORMANCE LOSS"
			);
		}

		TOAST_TRACE("VulkanCore", "  Device score: {}", device_score.total);

		if (device_score.total > best_score) {
			best_score = device_score.total;
			best_device = device;
			best_queue_families = queue_families;
		}
	}

	if (best_device == nullptr) {
		TOAST_CRITICAL("VulkanCore", "Toast Engine Error: No suitable Vulkan device found!");
	}

	m_physical_device = best_device;
	m_graphics_queue_family_index = best_queue_families.graphics;
	m_compute_queue_family_index = best_queue_families.compute;
	m_transfer_queue_family_index = best_queue_families.transfer;

	const auto selected_props = m_physical_device.getProperties();
	TOAST_INFO(
	    "VulkanCore",
	    "Selected device: {} (type: {}, API: {}) with score {}",
	    deviceNameString(selected_props),
	    deviceTypeToString(selected_props.deviceType),
	    versionToString(selected_props.apiVersion),
	    best_score
	);
}

void VulkanCore::createLogicalDeviceAndAllocator(std::span<const char* const> required_device_extensions) {
	if (m_graphics_queue_family_index == k_invalid_queue_family) {
		TOAST_CRITICAL("VulkanCore", "Failed to find a graphics queue family for the selected device!");
	}
	if (m_compute_queue_family_index == k_invalid_queue_family) {
		TOAST_CRITICAL("VulkanCore", "Failed to find a compute queue family for the selected device!");
	}
	if (m_transfer_queue_family_index == k_invalid_queue_family) {
		TOAST_CRITICAL("VulkanCore", "Failed to find a transfer queue family for the selected device!");
	}

	TOAST_TRACE(
	    "VulkanCore",
	    "Selected graphics queue family index: {} (flags: {})",
	    m_graphics_queue_family_index,
	    formatQueueFlags(m_physical_device.getQueueFamilyProperties()[m_graphics_queue_family_index].queueFlags)
	);
	TOAST_TRACE(
	    "VulkanCore",
	    "Selected compute queue family index: {} (flags: {})",
	    m_compute_queue_family_index,
	    formatQueueFlags(m_physical_device.getQueueFamilyProperties()[m_compute_queue_family_index].queueFlags)
	);
	TOAST_TRACE(
	    "VulkanCore",
	    "Selected transfer queue family index: {} (flags: {})",
	    m_transfer_queue_family_index,
	    formatQueueFlags(m_physical_device.getQueueFamilyProperties()[m_transfer_queue_family_index].queueFlags)
	);

	std::vector<uint32_t> unique_queue_families;
	auto push_unique = [&](uint32_t family_index) {
		if (family_index == k_invalid_queue_family) {
			return;
		}
		if (std::find(unique_queue_families.begin(), unique_queue_families.end(), family_index) == unique_queue_families.end()) {
			unique_queue_families.push_back(family_index);
		}
	};

	push_unique(m_graphics_queue_family_index);
	push_unique(m_compute_queue_family_index);
	push_unique(m_transfer_queue_family_index);

	float queue_priority = 1.0f;
	std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
	queue_create_infos.reserve(unique_queue_families.size());
	for (auto family_index : unique_queue_families) {
		queue_create_infos.emplace_back(vk::DeviceQueueCreateInfo({}, family_index, 1, &queue_priority));
	}

	std::vector<const char*> device_extensions(required_device_extensions.begin(), required_device_extensions.end());
	vk::DeviceCreateInfo device_ci({}, queue_create_infos, {}, device_extensions);
	vk::PhysicalDeviceVulkan12Features vulkan12_features {};
	vk::PhysicalDeviceVulkan13Features vulkan13_features {};
	vk::PhysicalDeviceVulkan11Features vulkan11_features {};
	vulkan12_features.pNext = &vulkan11_features;
	vulkan13_features.pNext = &vulkan12_features;
	vulkan13_features.dynamicRendering = VK_TRUE;
	vulkan13_features.synchronization2 = VK_TRUE;
	vulkan12_features.descriptorIndexing = VK_TRUE;
	vulkan12_features.runtimeDescriptorArray = VK_TRUE;
	vulkan12_features.descriptorBindingPartiallyBound = VK_TRUE;
	vulkan12_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
	vulkan12_features.bufferDeviceAddress = VK_TRUE;
	vulkan11_features.shaderDrawParameters = VK_TRUE;
	device_ci.pNext = &vulkan13_features;

	m_device = vk::raii::Device(m_physical_device, device_ci);

	m_graphics_queue = m_device.getQueue(m_graphics_queue_family_index, 0);
	m_compute_queue = m_device.getQueue(m_compute_queue_family_index, 0);
	m_transfer_queue = m_device.getQueue(m_transfer_queue_family_index, 0);

	vma::AllocatorCreateInfo allocator_ci {};
	allocator_ci.vulkanApiVersion = VK_API_VERSION_1_4;
	allocator_ci.physicalDevice = *m_physical_device;

	m_allocator.emplace(m_instance, m_device, allocator_ci);
}

auto VulkanCore::calculateDeviceScore(const vk::PhysicalDevice& device, std::span<const char* const> required_device_extensions)
    -> DeviceScore {
	DeviceScore score {};

	const auto props = device.getProperties();
	vk::PhysicalDeviceFeatures2 features2 {};
	vk::PhysicalDeviceVulkan12Features vulkan12_features {};
	vk::PhysicalDeviceVulkan13Features vulkan13_features {};
	vk::PhysicalDeviceVulkan11Features vulkan11_features {};
	vulkan12_features.pNext = &vulkan11_features;
	vulkan13_features.pNext = &vulkan12_features;
	features2.pNext = &vulkan13_features;
	device.getFeatures2(&features2);
	const auto features = device.getFeatures();
	const auto memory_props = device.getMemoryProperties();
	const auto extensions = device.enumerateDeviceExtensionProperties();
	const auto queue_families = device.getQueueFamilyProperties();
	const auto selected_queues = selectQueueFamilies(device);

	switch (props.deviceType) {
		case vk::PhysicalDeviceType::eDiscreteGpu: score.device_type = 1000; break;
		case vk::PhysicalDeviceType::eIntegratedGpu: score.device_type = 100; break;
		case vk::PhysicalDeviceType::eVirtualGpu: score.device_type = 10; break;
		case vk::PhysicalDeviceType::eCpu: score.device_type = -100; break;
		default: score.device_type = 0; break;
	}

	std::size_t total_memory = 0;
	for (uint32_t i = 0; i < memory_props.memoryHeapCount; ++i) {
		const auto& heap = memory_props.memoryHeaps[i];
		if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
			total_memory += static_cast<std::size_t>(heap.size);
		}
	}
	if (total_memory > 12ull * k_gigabyte_bytes) {
		// clamp to 12GB
		score.memory = 1200;
	} else if (total_memory > 0) {
		score.memory = static_cast<int>(total_memory / k_gigabyte_bytes) * 100;
	} else {
		score.memory = 0;
	}

	const auto& limits = props.limits;

	int texture_score = static_cast<int>(std::min(limits.maxImageDimension2D, 16384u) / 1024);
	texture_score = std::min(texture_score, 300);

	int vertex_attr_score = static_cast<int>(limits.maxVertexInputAttributes) / 32;
	vertex_attr_score = std::min(vertex_attr_score, 100);

	int push_const_score = static_cast<int>(limits.maxPushConstantsSize) / 256;
	push_const_score = std::min(push_const_score, 100);

	int ubo_score = static_cast<int>(limits.maxPerStageDescriptorUniformBuffers) / 8;
	ubo_score = std::min(ubo_score, 100);

	int ssbo_score = static_cast<int>(limits.maxPerStageDescriptorStorageBuffers) / 4;
	ssbo_score = std::min(ssbo_score, 100);

	score.limits = texture_score + vertex_attr_score + push_const_score + ubo_score + ssbo_score;
	score.limits = std::min(score.limits, 1000);

	int feature_score = 0;

	if (features.samplerAnisotropy) {
		feature_score += 200;
	}
	if (features.geometryShader) {
		feature_score += 300;
	}
	if (features.tessellationShader) {
		feature_score += 400;
	}

	// Important features
	if (features.multiDrawIndirect) {
		feature_score += 500;
	}
	if (features.independentBlend) {
		feature_score += 200;
	}
	if (features.fullDrawIndexUint32) {
		feature_score += 100;
	}

	int extra_features = 0;
	if (features.logicOp) {
		extra_features += 100;
	}
	if (features.pipelineStatisticsQuery) {
		extra_features += 100;
	}
	if (features.vertexPipelineStoresAndAtomics) {
		extra_features += 50;
	}
	feature_score += std::min(extra_features, 100);

	score.features = std::min(feature_score, 500);

	int extension_score = 0;
	std::vector<std::string> required_missing_names;

	const bool supports_dynamic_rendering = props.apiVersion >= VK_API_VERSION_1_3 && vulkan13_features.dynamicRendering == VK_TRUE;
	if (!supports_dynamic_rendering) {
		required_missing_names.emplace_back("Vulkan 1.3 dynamic rendering");
	} else {
		extension_score += 100;
	}

	const bool supports_sync2 = props.apiVersion >= VK_API_VERSION_1_3 && vulkan13_features.synchronization2 == VK_TRUE;
	if (!supports_sync2) {
		required_missing_names.emplace_back("Vulkan 1.3 synchronization2");
	} else {
		extension_score += 75;
	}

	const bool supports_descriptor_indexing =
	    props.apiVersion >= VK_API_VERSION_1_2 && vulkan12_features.descriptorIndexing == VK_TRUE;
	if (!supports_descriptor_indexing) {
		required_missing_names.emplace_back("Vulkan 1.2 descriptor indexing");
	} else {
		extension_score += 75;
	}

	const bool supports_runtime_descriptor_array =
	    props.apiVersion >= VK_API_VERSION_1_2 && vulkan12_features.runtimeDescriptorArray == VK_TRUE;
	if (!supports_runtime_descriptor_array) {
		required_missing_names.emplace_back("Vulkan 1.2 runtime descriptor arrays");
	} else {
		extension_score += 50;
	}

	const bool supports_partially_bound =
	    props.apiVersion >= VK_API_VERSION_1_2 && vulkan12_features.descriptorBindingPartiallyBound == VK_TRUE;
	if (!supports_partially_bound) {
		required_missing_names.emplace_back("Vulkan 1.2 partially bound descriptors");
	} else {
		extension_score += 25;
	}

	const bool supports_variable_descriptor_count =
	    props.apiVersion >= VK_API_VERSION_1_2 && vulkan12_features.descriptorBindingVariableDescriptorCount == VK_TRUE;
	if (!supports_variable_descriptor_count) {
		required_missing_names.emplace_back("Vulkan 1.2 variable descriptor count");
	} else {
		extension_score += 25;
	}

	const bool supports_buffer_device_address =
	    props.apiVersion >= VK_API_VERSION_1_2 && vulkan12_features.bufferDeviceAddress == VK_TRUE;
	if (!supports_buffer_device_address) {
		required_missing_names.emplace_back("Vulkan 1.2 buffer device address");
	} else {
		extension_score += 75;
	}

	for (const auto* required : required_device_extensions) {
		bool found = false;
		for (const auto& available : extensions) {
			if (std::string(required) == available.extensionName) {
				found = true;
				break;
			}
		}
		if (!found) {
			required_missing_names.emplace_back(required);
		} else {
			extension_score += 50;
		}
	}
	extension_score -= static_cast<int>(required_missing_names.size()) * 100;

	const std::vector<const char*> optional_extensions = {
	  VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
	  VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
	  VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
	  VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME
	};

	for (const auto* ext : optional_extensions) {
		for (const auto& available : extensions) {
			if (std::string(ext) == available.extensionName) {
				extension_score += 10;
				break;
			}
		}
	}

	score.extensions = std::max(std::min(extension_score, 500), -500);

	uint32_t vulkan_version = props.apiVersion;
	int version_score = 0;

	switch (VK_VERSION_MINOR(vulkan_version)) {
		case 0: version_score = 0; break;
		case 1: version_score = 25; break;
		case 2: version_score = 50; break;
		default: version_score = 100; break;
	}

	score.vulkan_support = version_score;

	int queue_score = 0;
	if (selected_queues.isComplete()) {
		score.graphics_idx = static_cast<int>(selected_queues.graphics);
		score.compute_idx = static_cast<int>(selected_queues.compute);
		score.transfer_idx = static_cast<int>(selected_queues.transfer);

		queue_score += 100;
		if ((queue_families[selected_queues.graphics].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags {}) {
			queue_score += 100;
		}
		if ((queue_families[selected_queues.compute].queueFlags & vk::QueueFlagBits::eCompute) != vk::QueueFlags {}) {
			queue_score += 150;
		}
		if ((queue_families[selected_queues.transfer].queueFlags & vk::QueueFlagBits::eTransfer) != vk::QueueFlags {}) {
			queue_score += 100;
		}

		queue_score += selected_queues.isDistinct() ? 250 : -500;
	} else {
		score.graphics_idx = -1;
		score.compute_idx = -1;
		score.transfer_idx = -1;
		queue_score -= 1000;
	}

	score.queue_support = queue_score;

	score.total = score.device_type + score.memory + score.limits + score.features + score.extensions + score.vulkan_support +
	              score.queue_support;

	score.missing_extensions = required_missing_names;

	return score;
}

bool VulkanCore::checkValidationLayerSupport() {
	{
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		return false;
	}
}
}
