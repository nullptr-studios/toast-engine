/**
 * @file descriptor_writer.hpp
 * @author dario
 * @date 14/08/2026
 */

#pragma once

#include "vulkan_common.hpp"

#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace renderer {

class DescriptorWriter {
public:
	auto buffer(
	    vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type, vk::Buffer buffer, vk::DeviceSize range = VK_WHOLE_SIZE,
	    vk::DeviceSize offset = 0
	) -> DescriptorWriter&;

	auto image(
	    vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type, vk::Sampler sampler, vk::ImageView view,
	    vk::ImageLayout layout
	) -> DescriptorWriter&;

	auto
	    imageArray(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type, std::span<const vk::DescriptorImageInfo> infos)
	        -> DescriptorWriter&;

	auto accelerationStructure(vk::DescriptorSet set, uint32_t binding, vk::AccelerationStructureKHR structure)
	    -> DescriptorWriter&;

	void flush(const vk::raii::Device& device);

private:
	std::vector<vk::WriteDescriptorSet> m_writes;

	// Deques since writes point into these and a deque never moves its elements
	std::deque<vk::DescriptorBufferInfo> m_buffer_infos;
	std::deque<std::vector<vk::DescriptorImageInfo>> m_image_infos;
	std::deque<vk::AccelerationStructureKHR> m_structures;
	std::deque<vk::WriteDescriptorSetAccelerationStructureKHR> m_structure_writes;
};

}
