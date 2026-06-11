#pragma once

#include <vulkan-memory-allocator-hpp/vk_mem_alloc_raii.hpp>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct FrameResources {
	std::optional<vma::raii::Buffer> stagingBuffer;
	std::optional<vma::raii::Buffer> gpuBuffer;
	vk::raii::DescriptorSet descriptorSet = nullptr;
};
