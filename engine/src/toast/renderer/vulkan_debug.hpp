/// @file vulkan_debug.hpp
/// @author dario
/// @date 09/07/2026

#pragma once
#include "vulkan_core.hpp"

#include <string>
#include <string_view>
#include <toast/log.hpp>

namespace renderer {

template<typename VkHandle>
void setDebugName(const VulkanCore& core, const VkHandle& handle, std::string_view name) {
	if (!core.debugUtilsEnabled() || !handle) {
		return;
	}

	try {
		core.getDevice().setDebugUtilsObjectNameEXT(handle, std::string(name));
	} catch (const std::exception& e) { TOAST_WARN("Render", "Failed to set debug name '{}': {}", name, e.what()); }
}

}
