#include "voxel_gpu_storage.hpp"

#include "vulkan_debug.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>
#include <toast/log.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace renderer {

namespace {

std::atomic<uint64_t> g_voxel_storage_generation {0};

}

VoxelGpuStorage::VoxelGpuStorage(std::vector<uint64_t> node_uids, std::vector<glm::uvec3> brick_dims)
    : m_node_uids(std::move(node_uids)),
      m_brick_dims(std::move(brick_dims)),
      m_generation(g_voxel_storage_generation.fetch_add(1, std::memory_order_relaxed) + 1) { }

auto makeVoxelInstance(const glm::mat4& model, const glm::mat4& inverse_model, uint32_t record_index) -> VoxelInstanceGpu {
	return {
	  .voxel_to_world = model * glm::scale(glm::mat4(1.0f), glm::vec3(voxel::k_voxel_size)),
	  .world_to_voxel = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / voxel::k_voxel_size)) * inverse_model,
	  .record_index = record_index,
	};
}

void writeVoxelStorageDescriptors(const vk::raii::Device& device, vk::DescriptorSet set, const VoxelGpuStorage& storage) {
	ZoneScoped;
	using Section = VoxelGpuStorage::Section;
	constexpr std::array k_sections {
	  Section::materials, Section::occupancy, Section::grids, Section::coarse, Section::palettes, Section::records
	};

	std::array<vk::DescriptorBufferInfo, VoxelGpuStorage::k_section_count> infos {};
	std::array<vk::WriteDescriptorSet, VoxelGpuStorage::k_section_count> writes {};
	for (size_t i = 0; i < k_sections.size(); ++i) {
		infos[i] = vk::DescriptorBufferInfo(storage.buffer(k_sections[i]), 0, storage.size(k_sections[i]));
		writes[i] =
		    vk::WriteDescriptorSet(set, static_cast<uint32_t>(i), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &infos[i]);
	}
	device.updateDescriptorSets(writes, {});
}

auto VoxelGpuStorage::recordIndexOf(uint64_t node_uid) const -> std::optional<uint32_t> {
	const auto found = std::ranges::find(m_node_uids, node_uid);
	if (found == m_node_uids.end()) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(found - m_node_uids.begin());
}

auto VoxelGpuStorage::buffer(Section section) const -> vk::Buffer {
	const auto& slot = m_buffers[static_cast<size_t>(section)];
	return slot.has_value() ? **slot : vk::Buffer {};
}

VoxelSceneUpload::VoxelSceneUpload(std::shared_ptr<VoxelGpuStorage> storage, std::shared_ptr<const VoxelPackedScene> packed)
    : m_storage(std::move(storage)),
      m_packed(std::move(packed)) { }

void VoxelSceneUpload::build(const VulkanCore& core) {
	ZoneScoped;
	m_storage->markUploading();

	const voxel::gpu::PackedPool& pool = m_packed->pool;
	const voxel::gpu::PackedScene& scene = m_packed->scene;
	const std::array<std::pair<const void*, vk::DeviceSize>, VoxelGpuStorage::k_section_count> sources {
	  {
	   {pool.materials.data(), pool.materials.size() * sizeof(uint32_t)},
	   {pool.occupancy.data(), pool.occupancy.size() * sizeof(uint32_t)},
	   {scene.grids.data(), scene.grids.size() * sizeof(uint32_t)},
	   {scene.coarse.data(), scene.coarse.size() * sizeof(uint32_t)},
	   {scene.palettes.data(), scene.palettes.size() * sizeof(uint32_t)},
	   {scene.records.data(), scene.records.size() * sizeof(voxel::gpu::VolumeRecord)},
	   }
	};
	constexpr std::array<std::string_view, VoxelGpuStorage::k_section_count> k_names {
	  "Materials", "Occupancy", "Grids", "Coarse", "Palettes", "Records"
	};

	// Storage buffers cannot be empty
	constexpr vk::DeviceSize k_min_bytes = sizeof(uint32_t);

	const bool concurrent = core.getGraphicsQueueFamilyIndex() != core.getTransferQueueFamilyIndex();
	const std::array family_indices {core.getGraphicsQueueFamilyIndex(), core.getTransferQueueFamilyIndex()};

	try {
		vk::DeviceSize total = 0;
		for (size_t i = 0; i < sources.size(); ++i) {
			m_offsets[i] = total;
			m_bytes[i] = sources[i].second;
			total += sources[i].second;
			m_storage->m_sizes[i] = std::max(sources[i].second, k_min_bytes);

			vk::BufferCreateInfo buffer_ci {};
			buffer_ci.size = m_storage->m_sizes[i];
			buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
			if (concurrent) {
				buffer_ci.sharingMode = vk::SharingMode::eConcurrent;
				buffer_ci.queueFamilyIndexCount = 2;
				buffer_ci.pQueueFamilyIndices = family_indices.data();
			} else {
				buffer_ci.sharingMode = vk::SharingMode::eExclusive;
			}

			vma::AllocationCreateInfo alloc_ci {};
			alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
			m_storage->m_buffers[i].emplace(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
			setDebugName(core, **m_storage->m_buffers[i], std::format("VoxelGpuStorage {}", k_names[i]));
		}

		if (total > 0) {
			vk::BufferCreateInfo staging_ci {};
			staging_ci.size = total;
			staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
			staging_ci.sharingMode = vk::SharingMode::eExclusive;

			vma::AllocationCreateInfo staging_alloc {};
			staging_alloc.usage = vma::MemoryUsage::eAuto;
			staging_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
			                      vma::AllocationCreateFlagBits::eHostAccessAllowTransferInstead;

			m_staging = core.getAllocator().createBuffer(staging_ci, staging_alloc);
			setDebugName(core, *m_staging, "VoxelGpuStorage Staging");
			host_bytes = total;

			auto* mapped = static_cast<uint8_t*>(m_staging.getAllocation().getInfo().pMappedData);
			if (mapped == nullptr) {
				TOAST_ERROR("Render", "Voxel staging buffer is not mapped; the voxel scene is not uploaded");
				m_storage->markFailed(IVulkanResource::UploadState::failed_gpu);
				return;
			}
			for (size_t i = 0; i < sources.size(); ++i) {
				if (m_bytes[i] > 0) {
					std::memcpy(mapped + m_offsets[i], sources[i].first, m_bytes[i]);
				}
			}
		}
	} catch (const std::exception& e) {
		TOAST_ERROR("Render", "Voxel scene upload failed to allocate: {}", e.what());
		m_storage->markFailed(IVulkanResource::UploadState::failed_gpu);
		return;
	}

	// Frees the packed scene unless a debug mirror still holds it
	m_packed.reset();
}

void VoxelSceneUpload::record(vk::CommandBuffer cmd) {
	ZoneScoped;
	if (m_storage->hasFailed()) {
		return;
	}

	for (size_t i = 0; i < VoxelGpuStorage::k_section_count; ++i) {
		if (m_bytes[i] == 0 || !m_storage->m_buffers[i].has_value()) {
			continue;
		}
		cmd.copyBuffer(*m_staging, **m_storage->m_buffers[i], vk::BufferCopy(m_offsets[i], 0, m_bytes[i]));
	}
}

}
