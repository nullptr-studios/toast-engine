/// @file voxel_blas_cache.cpp
/// @author dario
/// @date 25/10/2026

#include "toast/renderer/voxel/voxel_blas_cache.hpp"

#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <toast/log.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

auto aabbGeometry(vk::DeviceAddress aabbs) -> vk::AccelerationStructureGeometryKHR {
	vk::AccelerationStructureGeometryAabbsDataKHR data {};
	data.data.deviceAddress = aabbs;
	data.stride = sizeof(vk::AabbPositionsKHR);

	vk::AccelerationStructureGeometryKHR geometry {};
	geometry.geometryType = vk::GeometryTypeKHR::eAabbs;
	geometry.geometry.aabbs = data;
	geometry.flags = vk::GeometryFlagBitsKHR::eOpaque | vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
	return geometry;
}

auto alignUp(vk::DeviceSize value, vk::DeviceSize alignment) -> vk::DeviceSize {
	return (value + alignment - 1) / alignment * alignment;
}

auto brickBox(uint32_t brick) -> vk::AabbPositionsKHR {
	constexpr uint32_t mask = (1u << k_brick_coord_bits) - 1u;
	const auto dim = static_cast<float>(voxel::k_brick_dim);
	const float x = static_cast<float>(brick & mask) * dim;
	const float y = static_cast<float>((brick >> k_brick_coord_bits) & mask) * dim;
	const float z = static_cast<float>(brick >> (2 * k_brick_coord_bits)) * dim;
	return {x, y, z, x + dim, y + dim, z + dim};
}

}

VoxelBlasCache::VoxelBlasCache(const VulkanCore& core, uint32_t frames_in_flight) : m_core(&core), m_frames(frames_in_flight) {
	vk::BufferCreateInfo pool_ci {};
	pool_ci.size = sizeof(uint32_t) * k_pool_bricks;
	pool_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
	vma::AllocationCreateInfo pool_alloc {};
	pool_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_pool = core.getAllocator().createBuffer(pool_ci, pool_alloc);
	setDebugName(core, *m_pool, "VoxelBlas BrickPool");
	m_free.emplace(0u, k_pool_bricks);
}

auto VoxelBlasCache::allocate(uint32_t count) -> std::optional<uint32_t> {
	for (auto it = m_free.begin(); it != m_free.end(); ++it) {
		if (it->second < count) {
			continue;
		}
		const uint32_t base = it->first;
		const uint32_t rest = it->second - count;
		m_free.erase(it);
		if (rest > 0) {
			m_free.emplace(base + count, rest);
		}
		m_pool_used += count;
		return base;
	}
	return std::nullopt;
}

void VoxelBlasCache::release(uint32_t base, uint32_t count) {
	if (count == 0) {
		return;
	}
	m_pool_used -= count;
	auto next = m_free.lower_bound(base);
	if (next != m_free.end() && base + count == next->first) {
		count += next->second;
		next = m_free.erase(next);
	}
	if (next != m_free.begin()) {
		auto previous = std::prev(next);
		if (previous->first + previous->second == base) {
			previous->second += count;
			return;
		}
	}
	m_free.emplace(base, count);
}

void VoxelBlasCache::retire(FrameResources& frame, Entry& entry) {
	for (std::optional<Blas>* blas : {&entry.opaque, &entry.glass}) {
		if (blas->has_value()) {
			frame.retired.push_back(std::move(**blas));
			blas->reset();
		}
	}
}

auto VoxelBlasCache::createBlas(uint32_t count, vk::DeviceSize& scratch_size) -> std::optional<Blas> {
	const auto& device = m_core->getDevice();
	const vk::AccelerationStructureGeometryKHR geometry = aabbGeometry(0);
	vk::AccelerationStructureBuildGeometryInfoKHR build {};
	build.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	build.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	build.geometryCount = 1;
	build.pGeometries = &geometry;
	const auto sizes = device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build, count);

	Blas blas;
	vk::BufferCreateInfo storage_ci {};
	storage_ci.size = sizes.accelerationStructureSize;
	storage_ci.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	blas.storage = m_core->getAllocator().createBuffer(storage_ci, alloc_ci);
	setDebugName(*m_core, *blas.storage, "VoxelBlas Storage");

	vk::AccelerationStructureCreateInfoKHR as_ci {};
	as_ci.buffer = *blas.storage;
	as_ci.size = sizes.accelerationStructureSize;
	as_ci.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	blas.handle = vk::raii::AccelerationStructureKHR(device, as_ci);

	vk::AccelerationStructureDeviceAddressInfoKHR address_info {};
	address_info.accelerationStructure = *blas.handle;
	blas.address = device.getAccelerationStructureAddressKHR(address_info);
	blas.count = count;
	blas.bytes = sizes.accelerationStructureSize;
	scratch_size = sizes.buildScratchSize;
	return blas;
}

void VoxelBlasCache::update(
    vk::CommandBuffer cmd, uint32_t frame_index, const VoxelGpuStorage& storage, std::span<const uint32_t> records,
    uint32_t budget
) {
	ZoneScoped;
	if (frame_index >= m_frames.size()) {
		return;
	}
	FrameResources& frame = m_frames[frame_index];
	for (const Blas& blas : frame.retired) {
		release(blas.base, blas.count);
	}
	frame.retired.clear();
	++m_frame;
	Stats stats;

	m_jobs.clear();
	for (const uint32_t record : records) {
		const auto& list = storage.brickList(record);
		const uint64_t rid = storage.nodeRid(record);
		if (!list || rid == 0 || list->occupied.size() <= k_unit_box_bricks) {
			continue;
		}
		Entry& entry = m_entries[rid];
		if (entry.last_used == m_frame) {
			continue;
		}
		entry.last_used = m_frame;

		size_t dead = 0;
		if (entry.built) {
			const VoxelBrickList& built = *entry.built;
			const size_t total = built.occupied.size() + built.glass.size();
			dead = total - std::min(total, list->occupied.size() + list->glass.size());
			if (entry.checked != list->hash) {
				entry.checked = list->hash;
				entry.usable =
				    list->hash == built.hash || (built.dims == list->dims && std::ranges::includes(built.occupied, list->occupied) &&
						                             std::ranges::includes(built.glass, list->glass));
				entry.stale = entry.usable && list->hash != built.hash && dead * 10 >= total;
			}
		}
		if (!entry.built || !entry.usable) {
			m_jobs.push_back({.node_rid = rid, .list = list, .required = true});
		} else if (entry.stale) {
			m_jobs.push_back({.node_rid = rid, .list = list, .dead = dead});
			++stats.stale;
		}
	}
	std::ranges::stable_sort(m_jobs, [](const Job& a, const Job& b) {
		return a.required != b.required ? a.required : a.dead > b.dead;
	});

	size_t chosen = 0;
	uint64_t planned = 0;
	for (; chosen < m_jobs.size(); ++chosen) {
		const size_t bricks = m_jobs[chosen].list->occupied.size() + m_jobs[chosen].list->glass.size();
		if (chosen > 0 && planned + bricks > budget) {
			break;
		}
		planned += bricks;
	}
	for (size_t i = chosen; i < m_jobs.size(); ++i) {
		stats.waiting += m_jobs[i].required ? 1u : 0u;
	}

	if (chosen > 0) {
		const vk::DeviceSize coords_offset = alignUp(sizeof(vk::AabbPositionsKHR) * planned, 16);
		const vk::DeviceSize staging_size = coords_offset + (sizeof(uint32_t) * planned);
		if (!frame.staging.has_value() || frame.staging_size < staging_size) {
			frame.staging_size = std::max(staging_size, frame.staging_size + (frame.staging_size / 2));
			vk::BufferCreateInfo staging_ci {};
			staging_ci.size = frame.staging_size;
			staging_ci.usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
			                   vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc;
			vma::AllocationCreateInfo staging_alloc {};
			staging_alloc.usage = vma::MemoryUsage::eAuto;
			staging_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
			frame.staging.reset();
			frame.staging.emplace(m_core->getAllocator().createBuffer(staging_ci, staging_alloc));
			setDebugName(*m_core, **frame.staging, std::format("VoxelBlas Staging[{}]", frame_index));
		}
		auto* mapped = static_cast<std::byte*>(frame.staging->getAllocation().getInfo().pMappedData);
		auto* boxes = reinterpret_cast<vk::AabbPositionsKHR*>(mapped);
		auto* coords = reinterpret_cast<uint32_t*>(mapped + coords_offset);
		const vk::DeviceAddress staging_address = m_core->getBufferAddress(**frame.staging);

		struct Built {
			uint64_t node_rid = 0;
			std::shared_ptr<const VoxelBrickList> list;
			std::optional<Blas> opaque;
			std::optional<Blas> glass;
		};

		std::vector<Built> built;
		std::vector<vk::AccelerationStructureGeometryKHR> geometries;
		std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> infos;
		std::vector<vk::AccelerationStructureBuildRangeInfoKHR> ranges;
		std::vector<vk::DeviceSize> scratch_offsets;
		std::vector<vk::BufferCopy> copies;
		built.reserve(chosen);
		geometries.reserve(2 * chosen);
		infos.reserve(2 * chosen);
		ranges.reserve(2 * chosen);
		scratch_offsets.reserve(2 * chosen);
		const vk::DeviceSize scratch_alignment = m_core->getScratchAllocationSize(0);
		vk::DeviceSize scratch_total = 0;
		uint32_t written = 0;

		const auto add = [&](std::span<const uint32_t> bricks) -> std::optional<Blas> {
			const auto count = static_cast<uint32_t>(bricks.size());
			const std::optional<uint32_t> base = allocate(count);
			if (!base.has_value()) {
				return std::nullopt;
			}
			vk::DeviceSize scratch_size = 0;
			std::optional<Blas> blas = createBlas(count, scratch_size);
			if (!blas.has_value()) {
				release(*base, count);
				return std::nullopt;
			}
			blas->base = *base;
			for (uint32_t i = 0; i < count; ++i) {
				boxes[written + i] = brickBox(bricks[i]);
			}
			std::memcpy(coords + written, bricks.data(), sizeof(uint32_t) * count);
			copies.emplace_back(coords_offset + (sizeof(uint32_t) * written), sizeof(uint32_t) * *base, sizeof(uint32_t) * count);

			geometries.push_back(aabbGeometry(staging_address + (sizeof(vk::AabbPositionsKHR) * written)));
			vk::AccelerationStructureBuildGeometryInfoKHR info {};
			info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
			info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
			info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
			info.geometryCount = 1;
			info.pGeometries = &geometries.back();
			info.dstAccelerationStructure = *blas->handle;
			infos.push_back(info);
			vk::AccelerationStructureBuildRangeInfoKHR range {};
			range.primitiveCount = count;
			ranges.push_back(range);
			scratch_offsets.push_back(scratch_total);
			scratch_total = alignUp(scratch_total + scratch_size, scratch_alignment);
			written += count;
			return blas;
		};

		for (size_t j = 0; j < chosen; ++j) {
			const Job& job = m_jobs[j];
			Built entry {.node_rid = job.node_rid, .list = job.list};
			entry.opaque = add(job.list->occupied);
			if (entry.opaque.has_value() && !job.list->glass.empty()) {
				entry.glass = add(job.list->glass);
			}
			if (!entry.opaque.has_value() || (!job.list->glass.empty() && !entry.glass.has_value())) {
				// Built but never referenced so it is freed with this slot
				for (std::optional<Blas>* blas : {&entry.opaque, &entry.glass}) {
					if (blas->has_value()) {
						frame.retired.push_back(std::move(**blas));
					}
				}
				++stats.pool_full;
				continue;
			}
			stats.built_bricks += entry.opaque->count + (entry.glass.has_value() ? entry.glass->count : 0u);
			built.push_back(std::move(entry));
		}

		if (!infos.empty()) {
			if (!frame.scratch.has_value() || frame.scratch_size < m_core->getScratchAllocationSize(scratch_total)) {
				frame.scratch_size =
				    m_core->getScratchAllocationSize(std::max(scratch_total, frame.scratch_size + (frame.scratch_size / 2)));
				vk::BufferCreateInfo scratch_ci {};
				scratch_ci.size = frame.scratch_size;
				scratch_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
				vma::AllocationCreateInfo scratch_alloc {};
				scratch_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
				frame.scratch.reset();
				frame.scratch.emplace(m_core->getAllocator().createBuffer(scratch_ci, scratch_alloc));
				setDebugName(*m_core, **frame.scratch, std::format("VoxelBlas Scratch[{}]", frame_index));
			}
			const vk::DeviceAddress scratch_base = m_core->getAlignedScratchAddress(**frame.scratch);
			for (size_t i = 0; i < infos.size(); ++i) {
				infos[i].scratchData.deviceAddress = scratch_base + scratch_offsets[i];
			}
			frame.staging->getAllocation().flush(0, staging_size);

			cmd.copyBuffer(**frame.staging, *m_pool, copies);
			std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> range_ptrs;
			range_ptrs.reserve(ranges.size());
			for (const auto& range : ranges) {
				range_ptrs.push_back(reinterpret_cast<const VkAccelerationStructureBuildRangeInfoKHR*>(&range));
			}
			m_core->getDevice().getDispatcher()->vkCmdBuildAccelerationStructuresKHR(
			    static_cast<VkCommandBuffer>(cmd),
			    static_cast<uint32_t>(infos.size()),
			    reinterpret_cast<const VkAccelerationStructureBuildGeometryInfoKHR*>(infos.data()),
			    range_ptrs.data()
			);

			// The TLAS build that follows references these and every voxel ray reads the brick pool
			sync::BarrierBatch barriers;
			barriers.memory(sync::Usage::acceleration_build, sync::Usage::acceleration_build);
			barriers.memory(sync::Usage::transfer_dst, sync::Usage::compute_storage_read);
			barriers.memory(sync::Usage::transfer_dst, sync::Usage::fragment_storage_read);
			barriers.flush(cmd);
		}

		for (Built& result : built) {
			Entry& entry = m_entries[result.node_rid];
			retire(frame, entry);
			entry.opaque = std::move(result.opaque);
			entry.glass = std::move(result.glass);
			entry.lookup = Lookup {
			  .opaque = entry.opaque->address,
			  .glass = entry.glass.has_value() ? entry.glass->address : 0,
			  .opaque_base = entry.opaque->base,
			  .glass_base = entry.glass.has_value() ? entry.glass->base : 0,
			};
			entry.checked = result.list->hash;
			entry.built = std::move(result.list);
			entry.usable = true;
			entry.stale = false;
			++stats.builds;
		}
	}

	for (auto it = m_entries.begin(); it != m_entries.end();) {
		if (it->second.last_used + k_evict_frames < m_frame) {
			retire(frame, it->second);
			it = m_entries.erase(it);
			continue;
		}
		for (const std::optional<Blas>* blas : {&it->second.opaque, &it->second.glass}) {
			if (blas->has_value()) {
				++stats.blases;
				stats.bricks += (*blas)->count;
				stats.bytes += (*blas)->bytes;
			}
		}
		++it;
	}
	stats.pool_used = m_pool_used;

	std::scoped_lock lock(m_stats_mutex);
	m_stats = stats;
}

auto VoxelBlasCache::lookup(uint64_t node_rid) const -> const Lookup* {
	const auto found = m_entries.find(node_rid);
	if (found == m_entries.end() || !found->second.usable || !found->second.opaque.has_value()) {
		return nullptr;
	}
	return &found->second.lookup;
}

auto VoxelBlasCache::stats() const -> Stats {
	std::scoped_lock lock(m_stats_mutex);
	return m_stats;
}

}
