#include "voxel_gpu_storage.hpp"

#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>
#include <string_view>
#include <toast/log.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace renderer {

namespace {

std::atomic<uint64_t> g_voxel_storage_generation {0};

constexpr std::array<std::string_view, VoxelGpuStorage::k_section_count> k_section_names {
  "Materials", "Occupancy", "Grids", "Coarse", "Palettes", "Records"
};

/// Storage buffers cannot be empty
constexpr vk::DeviceSize k_min_section_bytes = sizeof(uint32_t);

constexpr uint32_t k_slot_headroom_divisor = 4;
constexpr uint32_t k_min_slot_headroom = 256;

/// Spare bytes past the packed grids coarse palettes and records for volumes that spawn later
constexpr std::array<vk::DeviceSize, 4> k_min_append_headroom {64 * 1024, 4 * 1024, 64 * 1024, 16 * 1024};

auto createSectionBuffer(const VulkanCore& core, vk::DeviceSize size, std::string_view name) -> vma::raii::Buffer {
	// The async compute family reads them for debris collision :3
	std::array<uint32_t, 3> family_indices {};
	uint32_t family_count = 0;
	for (const uint32_t family :
	     {core.getGraphicsQueueFamilyIndex(), core.getTransferQueueFamilyIndex(), core.asyncCompute().queueFamilyIndex()}) {
		if (std::find(family_indices.begin(), family_indices.begin() + family_count, family) ==
		    family_indices.begin() + family_count) {
			family_indices[family_count++] = family;
		}
	}
	const bool concurrent = family_count > 1;

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = size;
	buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
	if (concurrent) {
		buffer_ci.sharingMode = vk::SharingMode::eConcurrent;
		buffer_ci.queueFamilyIndexCount = family_count;
		buffer_ci.pQueueFamilyIndices = family_indices.data();
	} else {
		buffer_ci.sharingMode = vk::SharingMode::eExclusive;
	}

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	vma::raii::Buffer buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *buffer, std::format("VoxelGpuStorage {}", name));
	return buffer;
}

auto createStagingBuffer(const VulkanCore& core, vk::DeviceSize size) -> vma::raii::Buffer {
	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = size;
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
	staging_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo staging_alloc {};
	staging_alloc.usage = vma::MemoryUsage::eAuto;
	staging_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
	                      vma::AllocationCreateFlagBits::eHostAccessAllowTransferInstead;

	vma::raii::Buffer staging = core.getAllocator().createBuffer(staging_ci, staging_alloc);
	setDebugName(core, *staging, "VoxelGpuStorage Staging");
	return staging;
}

}

/// Section buffers shared by every version patched in place since the last full upload
class VoxelGpuSections {
public:
	std::array<std::optional<vma::raii::Buffer>, VoxelGpuStorage::k_section_count> buffers;
	std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> sizes {};

	uint32_t slot_capacity = 0;

	auto queuePatch(const VulkanCore& core, const voxel::gpu::ScenePatch& patch) -> std::optional<uint64_t>;
	void recordPending(vk::CommandBuffer cmd, uint64_t up_to, std::vector<std::shared_ptr<void>>& retired);

private:
	/// Every section in VoxelGpuStorage::Section order
	static constexpr size_t k_patched_sections = VoxelGpuStorage::k_section_count;

	struct Patch {
		uint64_t sequence = 0;
		std::shared_ptr<vma::raii::Buffer> staging;
		std::array<std::vector<vk::BufferCopy>, k_patched_sections> regions;
	};

	std::mutex m_mutex;
	std::deque<Patch> m_pending;
	uint64_t m_next_sequence = 0;
};

auto VoxelGpuSections::queuePatch(const VulkanCore& core, const voxel::gpu::ScenePatch& patch) -> std::optional<uint64_t> {
	ZoneScoped;
	using Section = VoxelGpuStorage::Section;
	const auto fits = [this](Section section, vk::DeviceSize bytes) { return bytes <= sizes[static_cast<size_t>(section)]; };
	if (patch.pool_slots > slot_capacity || !fits(Section::grids, vk::DeviceSize {patch.grid_words} * sizeof(uint32_t)) ||
	    !fits(Section::coarse, vk::DeviceSize {patch.coarse_words} * sizeof(uint32_t)) ||
	    !fits(Section::palettes, vk::DeviceSize {patch.palette_words} * sizeof(uint32_t)) ||
	    !fits(Section::records, vk::DeviceSize {patch.record_count} * sizeof(voxel::gpu::VolumeRecord))) {
		return std::nullopt;
	}

	const std::array<const voxel::gpu::PatchSection*, k_patched_sections> sections {
	  &patch.materials, &patch.occupancy, &patch.grids, &patch.coarse, &patch.palettes, &patch.records
	};

	Patch staged;
	vk::DeviceSize total = 0;
	std::array<vk::DeviceSize, k_patched_sections> offsets {};
	for (size_t i = 0; i < sections.size(); ++i) {
		offsets[i] = total;
		total += sections[i]->words.size() * sizeof(uint32_t);
		for (const voxel::gpu::PatchRun& run : sections[i]->runs) {
			staged.regions[i].emplace_back(
			    offsets[i] + (vk::DeviceSize {run.src} * sizeof(uint32_t)),
			    vk::DeviceSize {run.dst} * sizeof(uint32_t),
			    vk::DeviceSize {run.count} * sizeof(uint32_t)
			);
		}
	}

	if (total > 0) {
		try {
			staged.staging = std::make_shared<vma::raii::Buffer>(createStagingBuffer(core, total));
		} catch (const std::exception& e) {
			TOAST_WARN("Render", "Voxel patch staging failed to allocate, uploading the whole scene instead: {}", e.what());
			return std::nullopt;
		}
		auto* mapped = static_cast<uint8_t*>(staged.staging->getAllocation().getInfo().pMappedData);
		if (mapped == nullptr) {
			return std::nullopt;
		}
		for (size_t i = 0; i < sections.size(); ++i) {
			if (!sections[i]->words.empty()) {
				std::memcpy(mapped + offsets[i], sections[i]->words.data(), sections[i]->words.size() * sizeof(uint32_t));
			}
		}
	}

	const std::lock_guard lock(m_mutex);
	staged.sequence = ++m_next_sequence;
	const uint64_t sequence = staged.sequence;
	m_pending.push_back(std::move(staged));
	return sequence;
}

void VoxelGpuSections::recordPending(vk::CommandBuffer cmd, uint64_t up_to, std::vector<std::shared_ptr<void>>& retired) {
	std::vector<Patch> ready;
	{
		const std::lock_guard lock(m_mutex);
		while (!m_pending.empty() && m_pending.front().sequence <= up_to) {
			ready.push_back(std::move(m_pending.front()));
			m_pending.pop_front();
		}
	}
	if (ready.empty()) {
		return;
	}
	ZoneScoped;

	// Frames already submitted may still read the sections and later patches may rewrite the same words
	sync::BarrierBatch barriers;
	for (const Patch& patch : ready) {
		barriers.memory(sync::Usage::general, sync::Usage::transfer_dst);
		barriers.flush(cmd);
		for (size_t i = 0; i < k_patched_sections; ++i) {
			const auto& target = buffers[i];
			if (patch.staging != nullptr && !patch.regions[i].empty() && target.has_value()) {
				cmd.copyBuffer(**patch.staging, **target, patch.regions[i]);
			}
		}
		if (patch.staging != nullptr) {
			retired.push_back(patch.staging);
		}
	}
	barriers.memory(sync::Usage::transfer_dst, sync::Usage::general);
	barriers.flush(cmd);
}

VoxelGpuStorage::VoxelGpuStorage(std::vector<uint64_t> node_rids, std::vector<glm::uvec3> brick_dims)
    : m_node_rids(std::move(node_rids)),
      m_brick_dims(std::move(brick_dims)),
      m_sections(std::make_shared<VoxelGpuSections>()),
      m_generation(g_voxel_storage_generation.fetch_add(1, std::memory_order_relaxed) + 1) { }

VoxelGpuStorage::VoxelGpuStorage(
    std::vector<uint64_t> node_rids, std::vector<glm::uvec3> brick_dims, const VoxelGpuStorage& previous, uint64_t patch_sequence
)
    : m_node_rids(std::move(node_rids)),
      m_brick_dims(std::move(brick_dims)),
      m_sections(previous.m_sections),
      m_patch_sequence(patch_sequence),
      m_generation(g_voxel_storage_generation.fetch_add(1, std::memory_order_relaxed) + 1) {
	markReady();
}

auto VoxelGpuStorage::queuePatch(const VulkanCore& core, const voxel::gpu::ScenePatch& patch) const -> std::optional<uint64_t> {
	return isReady() ? m_sections->queuePatch(core, patch) : std::nullopt;
}

void VoxelGpuStorage::recordPendingPatches(vk::CommandBuffer cmd, std::vector<std::shared_ptr<void>>& retired) const {
	if (m_patch_sequence != 0) {
		m_sections->recordPending(cmd, m_patch_sequence, retired);
	}
}

auto makeVoxelInstance(
    const glm::mat4& model, const glm::mat4& inverse_model, const glm::mat4& previous_model, bool has_previous,
    uint32_t record_index
) -> VoxelInstanceGpu {
	const glm::mat4 voxel_scale = glm::scale(glm::mat4(1.0f), glm::vec3(voxel::k_voxel_size));
	return {
	  .voxel_to_world = model * voxel_scale,
	  .world_to_voxel = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / voxel::k_voxel_size)) * inverse_model,
	  .previous_voxel_to_world = previous_model * voxel_scale,
	  .record_index = record_index,
	  .flags = has_previous ? 0u : k_voxel_instance_no_motion,
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

auto VoxelGpuStorage::recordIndexOf(uint64_t node_rid) const -> std::optional<uint32_t> {
	const auto found = std::ranges::find(m_node_rids, node_rid);
	if (found == m_node_rids.end()) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(found - m_node_rids.begin());
}

auto VoxelGpuStorage::buffer(Section section) const -> vk::Buffer {
	const auto& slot = m_sections->buffers[static_cast<size_t>(section)];
	return slot.has_value() ? **slot : vk::Buffer {};
}

auto VoxelGpuStorage::size(Section section) const -> vk::DeviceSize {
	return m_sections->sizes[static_cast<size_t>(section)];
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

	VoxelGpuSections& sections = *m_storage->m_sections;

	// Spare room so later carves and spawns patch in place
	const auto slots = static_cast<uint32_t>(pool.materials.size() / voxel::gpu::k_material_words_per_brick);
	sections.slot_capacity = slots + std::max(slots / k_slot_headroom_divisor, k_min_slot_headroom);
	const std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> capacities {
	  vk::DeviceSize {sections.slot_capacity} * voxel::gpu::k_material_words_per_brick * sizeof(uint32_t),
	  vk::DeviceSize {sections.slot_capacity} * voxel::gpu::k_occupancy_words_per_brick * sizeof(uint32_t),
	  sources[2].second + std::max(sources[2].second / k_slot_headroom_divisor, k_min_append_headroom[0]),
	  sources[3].second + std::max(sources[3].second / k_slot_headroom_divisor, k_min_append_headroom[1]),
	  sources[4].second + std::max(sources[4].second / k_slot_headroom_divisor, k_min_append_headroom[2]),
	  sources[5].second + std::max(sources[5].second / k_slot_headroom_divisor, k_min_append_headroom[3]),
	};

	try {
		vk::DeviceSize total = 0;
		for (size_t i = 0; i < sources.size(); ++i) {
			m_offsets[i] = total;
			m_bytes[i] = sources[i].second;
			total += sources[i].second;
			sections.sizes[i] = std::max(capacities[i], k_min_section_bytes);
			sections.buffers[i].emplace(createSectionBuffer(core, sections.sizes[i], k_section_names[i]));
		}

		if (total > 0) {
			m_staging = createStagingBuffer(core, total);
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

namespace {

/// eConcurrent sections so the hand off is visibility only. General since shaders copies and patches all read them
auto voxelHandoff(sync::QueueHandoff handoff) -> sync::QueueHandoff {
	handoff.exclusive = false;
	return handoff;
}

void releaseSections(sync::BarrierBatch& batch, const VoxelGpuStorage& storage, const sync::QueueHandoff& handoff) {
	for (size_t i = 0; i < VoxelGpuStorage::k_section_count; ++i) {
		batch.release(
		    storage.buffer(static_cast<VoxelGpuStorage::Section>(i)),
		    sync::Usage::transfer_dst,
		    sync::Usage::general,
		    voxelHandoff(handoff)
		);
	}
}

void acquireSections(sync::BarrierBatch& batch, const VoxelGpuStorage& storage, const sync::QueueHandoff& handoff) {
	for (size_t i = 0; i < VoxelGpuStorage::k_section_count; ++i) {
		batch.acquire(storage.buffer(static_cast<VoxelGpuStorage::Section>(i)), sync::Usage::general, voxelHandoff(handoff));
	}
}

}

void VoxelSceneUpload::record(vk::CommandBuffer cmd) {
	ZoneScoped;
	if (m_storage->hasFailed()) {
		return;
	}

	const VoxelGpuSections& sections = *m_storage->m_sections;
	for (size_t i = 0; i < VoxelGpuStorage::k_section_count; ++i) {
		if (m_bytes[i] == 0 || !sections.buffers[i].has_value()) {
			continue;
		}
		cmd.copyBuffer(*m_staging, **sections.buffers[i], vk::BufferCopy(m_offsets[i], 0, m_bytes[i]));
	}

	sync::BarrierBatch barriers;
	releaseSections(barriers, *m_storage, m_handoff);
	barriers.flush(cmd);
	m_released = true;
}

void VoxelSceneUpload::recordAcquire(sync::BarrierBatch& batch) {
	if (m_released && !m_storage->hasFailed()) {
		acquireSections(batch, *m_storage, m_handoff);
	}
}

}
