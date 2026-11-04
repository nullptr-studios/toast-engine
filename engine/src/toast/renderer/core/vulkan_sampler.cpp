/// @file vulkan_sampler.cpp
/// @author dario

#include "toast/renderer/core/vulkan_sampler.hpp"

namespace renderer {

VulkanSampler::VulkanSampler(const VulkanCore& core, const vk::SamplerCreateInfo& info, std::string_view debug_name)
    : m_sampler(core.getDevice(), info) {
	if (!debug_name.empty()) {
		setDebugName(core, *m_sampler, debug_name);
	}
}

}
