/// @file vulkan_debug.hpp
/// @author dario
/// @date 09/07/2026.

#pragma once
#include "toast/log.hpp"
#include "vulkan_core.hpp"

#include <string>
#include <string_view>

namespace renderer {

/**
 * @brief Tags a Vulkan object with a debug name via VK_EXT_debug_utils
 *
 * @tparam VkHandle Any vulkan.hpp C++ handle wrapper
 */
template<typename VkHandle>
void setDebugName(const VulkanCore& core, const VkHandle& handle, std::string_view name) {
	if (!core.validationEnabled() || !handle) {
		return;
	}

	try {
		core.getDevice().setDebugUtilsObjectNameEXT(handle, std::string(name));
	} catch (const std::exception& e) { TOAST_WARN("VulkanDebug", "Failed to set debug name '{}': {}", name, e.what()); }
}

}
