#pragma once

#include <vulkan-memory-allocator-hpp/vk_mem_alloc_raii.hpp>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct FrameResources {
	std::optional<vma::raii::Buffer> staging_buffer;
	std::optional<vma::raii::Buffer> gpu_buffer;
	vk::raii::DescriptorSet descriptor_set = nullptr;
};
