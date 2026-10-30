/// @file voxel_trace_scene.cpp
/// @author dario
/// @date 21/10/2026

#include "toast/renderer/voxel/voxel_trace_scene.hpp"

#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/raytracing/ray_tracing_scene.hpp"
#include "toast/renderer/voxel/voxel_blas_cache.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

auto mappedBuffer(const VulkanCore& core, vk::DeviceSize size, vk::BufferUsageFlags usage, std::string_view name)
    -> vma::raii::Buffer {
	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = size;
	buffer_ci.usage = usage;
	buffer_ci.sharingMode = vk::SharingMode::eExclusive;

	// Every marched ray reads these so device local host visible when there is any
	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
	vma::raii::Buffer buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *buffer, name);
	return buffer;
}

template<typename T>
auto mapped(const vma::raii::Buffer& buffer) -> T* {
	return static_cast<T*>(buffer.getAllocation().getInfo().pMappedData);
}

/// The geometry data is a union that defaults to triangles so the aabbs member needs its own sType
auto boxGeometry(vk::DeviceAddress aabb) -> vk::AccelerationStructureGeometryKHR {
	vk::AccelerationStructureGeometryAabbsDataKHR aabbs {};
	aabbs.data.deviceAddress = aabb;
	aabbs.stride = sizeof(vk::AabbPositionsKHR);

	vk::AccelerationStructureGeometryKHR geometry {};
	geometry.geometryType = vk::GeometryTypeKHR::eAabbs;
	geometry.geometry.aabbs = aabbs;
	geometry.flags = vk::GeometryFlagBitsKHR::eOpaque | vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
	return geometry;
}

}

VoxelTraceScene::VoxelTraceScene(const VulkanCore& core, uint32_t frames_in_flight) : m_core(&core) {
	ZoneScoped;
	m_frames.resize(frames_in_flight);
	for (uint32_t i = 0; i < frames_in_flight; ++i) {
		FrameResources& frame = m_frames[i];
		frame.volumes = mappedBuffer(
		    core,
		    sizeof(VoxelInstanceGpu) * k_max_volumes,
		    vk::BufferUsageFlagBits::eStorageBuffer,
		    std::format("VoxelTrace Volumes[{}]", i)
		);
		frame.bounds = mappedBuffer(
		    core, sizeof(glm::vec4) * k_max_volumes, vk::BufferUsageFlagBits::eStorageBuffer, std::format("VoxelTrace Bounds[{}]", i)
		);
		frame.grid = mappedBuffer(
		    core, sizeof(uint32_t) * k_grid_words, vk::BufferUsageFlagBits::eStorageBuffer, std::format("VoxelTrace Grid[{}]", i)
		);
		frame.brick_bases = mappedBuffer(
		    core,
		    sizeof(glm::uvec4) * k_max_volumes,
		    vk::BufferUsageFlagBits::eStorageBuffer,
		    std::format("VoxelTrace BrickBases[{}]", i)
		);
		if (auto* words = mapped<uint32_t>(frame.grid)) {
			std::fill_n(words, k_header_words, 0u);
			frame.grid.getAllocation().flush(0, sizeof(uint32_t) * k_header_words);
		}
	}
	m_cell_counts.resize(static_cast<size_t>(k_grid_dim) * k_grid_dim * k_grid_dim);
	m_cell_cursor.resize(m_cell_counts.size());

	if (core.isRayTracingSupported()) {
		createUnitBox();
	}
}

void VoxelTraceScene::update(uint32_t frame_index, std::span<const Volume> volumes, glm::vec3 camera_position, float cell_size) {
	ZoneScoped;
	if (frame_index >= m_frames.size()) {
		return;
	}
	FrameResources& frame = m_frames[frame_index];

	m_volumes.assign(volumes.begin(), volumes.begin() + std::min<size_t>(volumes.size(), k_max_volumes));

	auto* instances = mapped<VoxelInstanceGpu>(frame.volumes);
	auto* bounds = mapped<glm::vec4>(frame.bounds);
	if (instances == nullptr || bounds == nullptr) {
		m_volumes.clear();
	}
	for (size_t i = 0; i < m_volumes.size(); ++i) {
		instances[i] = m_volumes[i].instance;
		bounds[i] = m_volumes[i].bounds;
	}
	if (!m_volumes.empty()) {
		frame.volumes.getAllocation().flush(0, sizeof(VoxelInstanceGpu) * m_volumes.size());
		frame.bounds.getAllocation().flush(0, sizeof(glm::vec4) * m_volumes.size());
	}

	writeGrid(frame, camera_position, cell_size);
}

void VoxelTraceScene::writeGrid(FrameResources& frame, glm::vec3 camera_position, float cell_size) {
	ZoneScoped;
	auto* words = mapped<uint32_t>(frame.grid);
	if (words == nullptr) {
		return;
	}

	const float cell = std::max(cell_size, 0.25f);
	const auto dim = static_cast<int32_t>(k_grid_dim);
	const glm::vec3 origin = (glm::floor(camera_position / cell) - glm::vec3(static_cast<float>(dim / 2.0))) * cell;

	struct Span {
		glm::ivec3 lo;
		glm::ivec3 hi;
	};

	std::vector<Span> spans(m_volumes.size());
	std::vector<uint32_t> far;
	std::vector<uint32_t> glass;
	std::ranges::fill(m_cell_counts, 0u);

	const auto cell_index = [dim](int32_t x, int32_t y, int32_t z) {
		return static_cast<size_t>(x) + (static_cast<size_t>(y) * dim) + (static_cast<size_t>(z) * dim * dim);
	};

	// Volumes leaving the window or covering too many cells are tested by every ray instead
	uint64_t entries = 0;
	for (size_t i = 0; i < m_volumes.size(); ++i) {
		const glm::vec4 b = m_volumes[i].bounds;
		const glm::ivec3 lo(glm::floor((glm::vec3(b) - b.w - origin) / cell));
		const glm::ivec3 hi(glm::floor((glm::vec3(b) + b.w - origin) / cell));
		const glm::ivec3 span = hi - lo + 1;
		const uint64_t cells = static_cast<uint64_t>(span.x) * span.y * span.z;
		const bool inside = glm::all(glm::greaterThanEqual(lo, glm::ivec3(0))) && glm::all(glm::lessThan(hi, glm::ivec3(dim)));
		if (m_volumes[i].glass) {
			glass.push_back(static_cast<uint32_t>(i));
		}
		if (!inside || cells > k_max_cells_per_volume || entries + cells > k_max_entries) {
			far.push_back(static_cast<uint32_t>(i));
			spans[i] = {.lo = glm::ivec3(0), .hi = glm::ivec3(-1)};
			continue;
		}
		spans[i] = {.lo = lo, .hi = hi};
		entries += cells;
		for (int32_t z = lo.z; z <= hi.z; ++z) {
			for (int32_t y = lo.y; y <= hi.y; ++y) {
				for (int32_t x = lo.x; x <= hi.x; ++x) {
					++m_cell_counts[cell_index(x, y, z)];
				}
			}
		}
	}

	const uint32_t cells_offset = k_header_words;
	const auto entries_offset = static_cast<uint32_t>(cells_offset + (2 * m_cell_counts.size()));
	uint32_t running = 0;
	for (size_t c = 0; c < m_cell_counts.size(); ++c) {
		words[cells_offset + (2 * c)] = running;
		words[cells_offset + (2 * c) + 1] = m_cell_counts[c];
		m_cell_cursor[c] = running;
		running += m_cell_counts[c];
	}
	for (size_t i = 0; i < spans.size(); ++i) {
		const Span& s = spans[i];
		for (int32_t z = s.lo.z; z <= s.hi.z; ++z) {
			for (int32_t y = s.lo.y; y <= s.hi.y; ++y) {
				for (int32_t x = s.lo.x; x <= s.hi.x; ++x) {
					words[entries_offset + m_cell_cursor[cell_index(x, y, z)]++] = static_cast<uint32_t>(i);
				}
			}
		}
	}

	const uint32_t far_offset = entries_offset + running;
	const auto glass_offset = static_cast<uint32_t>(far_offset + far.size());
	std::ranges::copy(far, words + far_offset);
	std::ranges::copy(glass, words + glass_offset);

	words[0] = std::bit_cast<uint32_t>(origin.x);
	words[1] = std::bit_cast<uint32_t>(origin.y);
	words[2] = std::bit_cast<uint32_t>(origin.z);
	words[3] = std::bit_cast<uint32_t>(cell);
	words[4] = k_grid_dim;
	words[5] = static_cast<uint32_t>(m_volumes.size());
	words[6] = static_cast<uint32_t>(far.size());
	words[7] = static_cast<uint32_t>(glass.size());
	words[8] = far_offset;
	words[9] = glass_offset;
	words[10] = entries_offset;
	std::fill(words + 11, words + k_header_words, 0u);

	frame.grid.getAllocation().flush(0, sizeof(uint32_t) * (glass_offset + glass.size()));
}

void VoxelTraceScene::createUnitBox() {
	ZoneScoped;
	const auto& device = m_core->getDevice();

	m_box_aabb.emplace(mappedBuffer(
	    *m_core,
	    sizeof(vk::AabbPositionsKHR),
	    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
	    "VoxelTrace UnitBox"
	));
	if (auto* box = mapped<vk::AabbPositionsKHR>(*m_box_aabb)) {
		*box = vk::AabbPositionsKHR(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
		m_box_aabb->getAllocation().flush(0, sizeof(vk::AabbPositionsKHR));
	}

	const vk::AccelerationStructureGeometryKHR geometry = boxGeometry(m_core->getBufferAddress(**m_box_aabb));

	vk::AccelerationStructureBuildGeometryInfoKHR build {};
	build.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	build.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	build.geometryCount = 1;
	build.pGeometries = &geometry;
	const auto sizes = device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build, 1u);

	vk::BufferCreateInfo storage_ci {};
	storage_ci.size = sizes.accelerationStructureSize;
	storage_ci.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	vma::AllocationCreateInfo device_alloc {};
	device_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_box_storage.emplace(m_core->getAllocator().createBuffer(storage_ci, device_alloc));

	vk::AccelerationStructureCreateInfoKHR as_ci {};
	as_ci.buffer = **m_box_storage;
	as_ci.size = sizes.accelerationStructureSize;
	as_ci.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	m_box = vk::raii::AccelerationStructureKHR(device, as_ci);
	setDebugName(*m_core, **m_box_storage, "VoxelTrace UnitBoxBLAS");

	vk::BufferCreateInfo scratch_ci {};
	scratch_ci.size = m_core->getScratchAllocationSize(sizes.buildScratchSize);
	scratch_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	m_box_scratch.emplace(m_core->getAllocator().createBuffer(scratch_ci, device_alloc));

	vk::AccelerationStructureDeviceAddressInfoKHR address_info {};
	address_info.accelerationStructure = *m_box;
	m_box_address = device.getAccelerationStructureAddressKHR(address_info);
}

auto VoxelTraceScene::addRayQueryInstances(
    vk::CommandBuffer cmd, uint32_t frame_index, RayTracingScene& scene, const VoxelBlasCache* bricks,
    const VoxelGpuStorage* storage
) -> bool {
	ZoneScoped;
	if (m_box_address == 0) {
		return false;
	}

	if (!m_box_built) {
		const vk::AccelerationStructureGeometryKHR geometry = boxGeometry(m_core->getBufferAddress(**m_box_aabb));

		vk::AccelerationStructureBuildGeometryInfoKHR build {};
		build.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		build.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
		build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
		build.geometryCount = 1;
		build.pGeometries = &geometry;
		build.dstAccelerationStructure = *m_box;
		build.scratchData.deviceAddress = m_core->getAlignedScratchAddress(**m_box_scratch);

		vk::AccelerationStructureBuildRangeInfoKHR range {};
		range.primitiveCount = 1;
		const auto* range_ptr = reinterpret_cast<const VkAccelerationStructureBuildRangeInfoKHR*>(&range);
		m_core->getDevice().getDispatcher()->vkCmdBuildAccelerationStructuresKHR(
		    static_cast<VkCommandBuffer>(cmd),
		    1,
		    reinterpret_cast<const VkAccelerationStructureBuildGeometryInfoKHR*>(&build),
		    &range_ptr
		);

		// The TLAS build that follows references it
		sync::BarrierBatch barriers;
		barriers.memory(sync::Usage::acceleration_build, sync::Usage::acceleration_build);
		barriers.flush(cmd);
		m_box_built = true;
	}

	auto* bases = frame_index < m_frames.size() ? mapped<glm::uvec4>(m_frames[frame_index].brick_bases) : nullptr;
	for (size_t i = 0; i < m_volumes.size(); ++i) {
		const Volume& volume = m_volumes[i];
		const VoxelBlasCache::Lookup* found = bricks != nullptr && storage != nullptr && bases != nullptr
		                                          ? bricks->lookup(storage->nodeRid(volume.instance.record_index))
		                                          : nullptr;
		if (found != nullptr) {
			const auto index = static_cast<uint32_t>(i) | VoxelBlasCache::k_instance_bricks;
			scene.addInstance(
			    RayTracingScene::Instance {
			      .transform = volume.instance.voxel_to_world,
			      .blas_address = found->opaque,
			      .custom_index = index,
			      .mask = k_mask_volume
			    }
			);
			if (found->glass != 0) {
				scene.addInstance(
				    RayTracingScene::Instance {
				      .transform = volume.instance.voxel_to_world,
				      .blas_address = found->glass,
				      .custom_index = index | VoxelBlasCache::k_instance_glass,
				      .mask = k_mask_glass,
				    }
				);
			}
			bases[i] = glm::uvec4(found->opaque_base, found->glass_base, volume.instance.record_index, 0u);
			continue;
		}
		scene.addInstance(
		    RayTracingScene::Instance {
		      .transform = volume.instance.voxel_to_world * glm::scale(glm::mat4(1.0f), glm::vec3(volume.voxel_dims)),
		      .blas_address = m_box_address,
		      .custom_index = static_cast<uint32_t>(i),
		      .mask = static_cast<uint8_t>(k_mask_volume | (volume.glass ? k_mask_glass : 0)),
		    }
		);
	}
	if (bases != nullptr && !m_volumes.empty()) {
		m_frames[frame_index].brick_bases.getAllocation().flush(0, sizeof(glm::uvec4) * m_volumes.size());
	}
	return true;
}

}
