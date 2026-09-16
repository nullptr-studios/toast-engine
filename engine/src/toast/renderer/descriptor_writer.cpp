/**
 * @file descriptor_writer.cpp
 * @author dario
 * @date 14/08/2026
 */

#include "descriptor_writer.hpp"

namespace renderer {

auto DescriptorWriter::buffer(
    vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type, vk::Buffer buffer, vk::DeviceSize range,
    vk::DeviceSize offset
) -> DescriptorWriter& {
	if (!set || !buffer) {
		return *this;
	}

	const auto& info = m_buffer_infos.emplace_back(buffer, offset, range);
	m_writes.emplace_back(set, binding, 0, 1, type, nullptr, &info);
	return *this;
}

auto DescriptorWriter::image(
    vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type, vk::Sampler sampler, vk::ImageView view,
    vk::ImageLayout layout
) -> DescriptorWriter& {
	if (!set || !view) {
		return *this;
	}

	const auto& infos = m_image_infos.emplace_back(1, vk::DescriptorImageInfo(sampler, view, layout));
	m_writes.emplace_back(set, binding, 0, 1, type, infos.data());
	return *this;
}

auto DescriptorWriter::imageArray(
    vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type, std::span<const vk::DescriptorImageInfo> infos
) -> DescriptorWriter& {
	if (!set || infos.empty()) {
		return *this;
	}

	// Copied since the write outlives the caller span until flush()
	const auto& stored = m_image_infos.emplace_back(infos.begin(), infos.end());
	m_writes.emplace_back(set, binding, 0, static_cast<uint32_t>(stored.size()), type, stored.data());
	return *this;
}

auto DescriptorWriter::accelerationStructure(vk::DescriptorSet set, uint32_t binding, vk::AccelerationStructureKHR structure)
    -> DescriptorWriter& {
	if (!set || !structure) {
		return *this;
	}

	const auto& stored = m_structures.emplace_back(structure);

	auto& structure_write = m_structure_writes.emplace_back();
	structure_write.accelerationStructureCount = 1;
	structure_write.pAccelerationStructures = &stored;

	vk::WriteDescriptorSet write {};
	write.pNext = &structure_write;
	write.dstSet = set;
	write.dstBinding = binding;
	write.descriptorCount = 1;
	write.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
	m_writes.push_back(write);
	return *this;
}

void DescriptorWriter::flush(const vk::raii::Device& device) {
	if (!m_writes.empty()) {
		device.updateDescriptorSets(m_writes, {});
	}

	m_writes.clear();
	m_buffer_infos.clear();
	m_image_infos.clear();
	m_structures.clear();
	m_structure_writes.clear();
}

}
