/// @file vulkan_sampler.hpp
/// @author dario

#pragma once

#include "vulkan_common.hpp"

#include <string_view>

namespace toast::renderer {
class VulkanCore;

/// @brief Owns a vk::raii::Sampler
class VulkanSampler {
public:
	VulkanSampler() = default;
	VulkanSampler(const VulkanCore& core, const vk::SamplerCreateInfo& info, std::string_view debug_name = {});

	[[nodiscard]]
	auto handle() const -> VkSampler {
		return *m_sampler;
	}

private:
	vk::raii::Sampler m_sampler = nullptr;
};

}
