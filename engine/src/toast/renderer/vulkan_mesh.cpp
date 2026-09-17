/// @file VulkanMesh.cpp
/// @author dario
/// @date 07/06/2026

#include "vulkan_mesh.hpp"

#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <format>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <type_traits>

namespace renderer {
static_assert(std::is_standard_layout_v<Vertex>, "Vertex must be standard layout");
static_assert(sizeof(Vertex) == 60, "Vertex size must match mesh.slang input layout (60 bytes)");
static_assert(offsetof(Vertex, position) == 0, "Vertex.position offset mismatch");
static_assert(offsetof(Vertex, normal) == 12, "Vertex.normal offset mismatch");
static_assert(offsetof(Vertex, uv) == 24, "Vertex.uv offset mismatch");
static_assert(offsetof(Vertex, tangent) == 32, "Vertex.tangent offset mismatch");
static_assert(offsetof(Vertex, color) == 48, "Vertex.color offset mismatch");

static_assert(std::is_standard_layout_v<SkinVertex>, "SkinVertex must be standard layout");
static_assert(sizeof(SkinVertex) == 24, "SkinVertex size must match the binding-1 stride (24 bytes)");
static_assert(offsetof(SkinVertex, joints) == 0, "SkinVertex.joints offset mismatch");
static_assert(offsetof(SkinVertex, weights) == 8, "SkinVertex.weights offset mismatch");

auto vertexBindingDescription() -> vk::VertexInputBindingDescription {
	return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
}

auto vertexAttributeDescriptions() -> std::array<vk::VertexInputAttributeDescription, 5> {
	return {
	  vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)),
	  vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)),
	  vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)),
	  vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, tangent)),
	  vk::VertexInputAttributeDescription(4, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color))
	};
}

void VulkanMesh::create(
    const renderer::VulkanCore& core, UploadData data, uint32_t graphics_queue_family_index, uint32_t transfer_queue_family_index,
    std::string_view debug_name
) {
	ZoneScoped;
	if (data.vertices.empty()) {
		TOAST_CRITICAL("Render", "Mesh has no vertices");
	}

	if (data.indices.empty()) {
		TOAST_CRITICAL("Render", "Mesh has no indices");
	}

	if (isReady()) {
		destroy();
	}

	m_vertex_count = static_cast<uint32_t>(data.vertices.size());
	m_index_count = static_cast<uint32_t>(data.indices.size());
	m_vertex_size = data.vertices.size_bytes();
	m_index_size = data.indices.size_bytes();
	m_skin_vertex_size = data.skin_vertices.size_bytes();

	const bool use_concurrent_sharing = graphics_queue_family_index != transfer_queue_family_index;

	// Unconditional since usage cannot be added after creation
	constexpr vk::BufferUsageFlags k_acceleration_structure_usage =
	    vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer |
	    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;

	vk::BufferCreateInfo vb_ci {};
	vb_ci.size = m_vertex_size;
	vb_ci.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer | k_acceleration_structure_usage;

	std::array family_indices {graphics_queue_family_index, transfer_queue_family_index};

	if (use_concurrent_sharing) {
		vb_ci.sharingMode = vk::SharingMode::eConcurrent;
		vb_ci.queueFamilyIndexCount = 2;
		vb_ci.pQueueFamilyIndices = family_indices.data();
	} else {
		vb_ci.sharingMode = vk::SharingMode::eExclusive;
	}

	vma::AllocationCreateInfo vb_alloc {};
	vb_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_vertex_buffer.emplace(core.getAllocator().createBuffer(vb_ci, vb_alloc));
	if (!debug_name.empty()) {
		setDebugName(core, **m_vertex_buffer, std::format("{} VertexBuffer", debug_name));
	}

	vk::BufferCreateInfo ib_ci {};
	ib_ci.size = m_index_size;
	ib_ci.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer | k_acceleration_structure_usage;

	if (use_concurrent_sharing) {
		ib_ci.sharingMode = vk::SharingMode::eConcurrent;
		ib_ci.queueFamilyIndexCount = 2;
		ib_ci.pQueueFamilyIndices = family_indices.data();
	} else {
		ib_ci.sharingMode = vk::SharingMode::eExclusive;
	}

	vma::AllocationCreateInfo ib_alloc {};
	ib_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_index_buffer.emplace(core.getAllocator().createBuffer(ib_ci, ib_alloc));
	if (!debug_name.empty()) {
		setDebugName(core, **m_index_buffer, std::format("{} IndexBuffer", debug_name));
	}

	if (m_skin_vertex_size > 0) {
		vk::BufferCreateInfo sb_ci {};
		sb_ci.size = m_skin_vertex_size;
		sb_ci.usage =
		    vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer;

		if (use_concurrent_sharing) {
			sb_ci.sharingMode = vk::SharingMode::eConcurrent;
			sb_ci.queueFamilyIndexCount = 2;
			sb_ci.pQueueFamilyIndices = family_indices.data();
		} else {
			sb_ci.sharingMode = vk::SharingMode::eExclusive;
		}

		vma::AllocationCreateInfo sb_alloc {};
		sb_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;

		m_skin_vertex_buffer.emplace(core.getAllocator().createBuffer(sb_ci, sb_alloc));
		if (!debug_name.empty()) {
			setDebugName(core, **m_skin_vertex_buffer, std::format("{} SkinVertexBuffer", debug_name));
		}
	} else {
		m_skin_vertex_buffer.reset();
	}
}

void VulkanMesh::destroy() {
	m_vertex_buffer.reset();
	m_index_buffer.reset();
	m_skin_vertex_buffer.reset();

	m_blas = nullptr;
	m_blas_buffer.reset();
	m_blas_address = 0;
	m_blas_scratch_address = 0;
	m_blas_primitive_count = 0;

	m_vertex_count = 0;
	m_index_count = 0;

	m_vertex_size = 0;
	m_index_size = 0;
	m_skin_vertex_size = 0;
}

void VulkanMesh::recordUpload(
    vk::CommandBuffer cmd, vk::Buffer staging_buffer, vk::DeviceSize vertex_offset, vk::DeviceSize index_offset,
    vk::DeviceSize skin_vertex_offset
) const {
	ZoneScoped;
	if (!m_vertex_buffer || !m_index_buffer) {
		TOAST_CRITICAL("Render", "Mesh buffers were not created before upload");
	}

	cmd.copyBuffer(staging_buffer, **m_vertex_buffer, vk::BufferCopy(vertex_offset, 0, m_vertex_size));
	cmd.copyBuffer(staging_buffer, **m_index_buffer, vk::BufferCopy(index_offset, 0, m_index_size));

	if (m_skin_vertex_buffer.has_value()) {
		cmd.copyBuffer(staging_buffer, **m_skin_vertex_buffer, vk::BufferCopy(skin_vertex_offset, 0, m_skin_vertex_size));
	}
}

void VulkanMesh::bind(vk::CommandBuffer cmd) const {
	cmd.bindVertexBuffers(0, {*m_vertex_buffer}, {0});

	cmd.bindIndexBuffer(*m_index_buffer, 0, vk::IndexType::eUint32);
}

void VulkanMesh::bindPosed(vk::CommandBuffer cmd, vk::Buffer posed_vertices, uint32_t posed_vertex_offset) const {
	// Offset on the binding not vertexOffset since indices are relative to vertex 0
	const vk::DeviceSize byte_offset = static_cast<vk::DeviceSize>(posed_vertex_offset) * sizeof(Vertex);
	cmd.bindVertexBuffers(0, {posed_vertices}, {byte_offset});

	cmd.bindIndexBuffer(*m_index_buffer, 0, vk::IndexType::eUint32);
}

void VulkanMesh::draw(vk::CommandBuffer cmd, uint32_t instance_count) const {
	if (!isReady() || instance_count == 0) {
		return;
	}

	// firstInstance stays 0 since the run start comes from a push constant
	cmd.drawIndexed(m_index_count, instance_count, 0, 0, 0);
}

MeshUpload::MeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data, assets::HandleBase source, std::string_view debug_name) {
	this->mesh = &mesh;
	this->data = data;
	this->source = std::move(source);
	this->debug_name = debug_name;
}

auto VulkanMesh::createAccelerationStructure(const VulkanCore& core) -> std::optional<vma::raii::Buffer> {
	ZoneScoped;
	if (!core.isRayTracingSupported() || !m_vertex_buffer.has_value() || !m_index_buffer.has_value()) {
		return std::nullopt;
	}

	const auto& device = core.getDevice();

	vk::AccelerationStructureGeometryTrianglesDataKHR triangles {};
	triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
	triangles.vertexData.deviceAddress = core.getBufferAddress(**m_vertex_buffer);
	triangles.vertexStride = sizeof(Vertex);
	triangles.maxVertex = m_vertex_count > 0 ? m_vertex_count - 1 : 0;
	triangles.indexType = vk::IndexType::eUint32;
	triangles.indexData.deviceAddress = core.getBufferAddress(**m_index_buffer);

	m_blas_geometry = vk::AccelerationStructureGeometryKHR {};
	m_blas_geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
	m_blas_geometry.geometry.triangles = triangles;
	// Opaque with no any-hit so cutouts trace solid
	m_blas_geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

	m_blas_primitive_count = m_index_count / 3;

	m_blas_build_info = vk::AccelerationStructureBuildGeometryInfoKHR {};
	m_blas_build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	m_blas_build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	m_blas_build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	m_blas_build_info.geometryCount = 1;
	m_blas_build_info.pGeometries = &m_blas_geometry;

	const auto sizes = device.getAccelerationStructureBuildSizesKHR(
	    vk::AccelerationStructureBuildTypeKHR::eDevice, m_blas_build_info, m_blas_primitive_count
	);

	vk::BufferCreateInfo as_buffer_ci {};
	as_buffer_ci.size = sizes.accelerationStructureSize;
	as_buffer_ci.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	as_buffer_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo as_alloc {};
	as_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_blas_buffer.emplace(core.getAllocator().createBuffer(as_buffer_ci, as_alloc));

	vk::AccelerationStructureCreateInfoKHR as_ci {};
	as_ci.buffer = **m_blas_buffer;
	as_ci.size = sizes.accelerationStructureSize;
	as_ci.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	m_blas = vk::raii::AccelerationStructureKHR(device, as_ci);

	m_blas_build_info.dstAccelerationStructure = *m_blas;

	vk::AccelerationStructureDeviceAddressInfoKHR address_info {};
	address_info.accelerationStructure = *m_blas;
	m_blas_address = device.getAccelerationStructureAddressKHR(address_info);

	vk::BufferCreateInfo scratch_ci {};
	scratch_ci.size = core.getScratchAllocationSize(sizes.buildScratchSize);
	scratch_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	scratch_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo scratch_alloc {};
	scratch_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	auto scratch = core.getAllocator().createBuffer(scratch_ci, scratch_alloc);

	m_blas_scratch_address = core.getAlignedScratchAddress(*scratch);
	m_build_acceleration_structures = device.getDispatcher()->vkCmdBuildAccelerationStructuresKHR;

	return scratch;
}

void VulkanMesh::recordBuildAccelerationStructure(vk::CommandBuffer cmd) const {
	ZoneScoped;
	if (*m_blas == VK_NULL_HANDLE || m_blas_scratch_address == 0 || m_build_acceleration_structures == nullptr) {
		return;
	}

	vk::AccelerationStructureBuildGeometryInfoKHR build = m_blas_build_info;
	build.pGeometries = &m_blas_geometry;
	build.scratchData.deviceAddress = m_blas_scratch_address;

	vk::AccelerationStructureBuildRangeInfoKHR range {};
	range.primitiveCount = m_blas_primitive_count;

	const auto* range_ptr = reinterpret_cast<const VkAccelerationStructureBuildRangeInfoKHR*>(&range);
	m_build_acceleration_structures(
	    static_cast<VkCommandBuffer>(cmd),
	    1,
	    reinterpret_cast<const VkAccelerationStructureBuildGeometryInfoKHR*>(&build),
	    &range_ptr
	);
}

void MeshUpload::build(const VulkanCore& core) {
	ZoneScoped;
	mesh->create(core, data, core.getGraphicsQueueFamilyIndex(), core.getTransferQueueFamilyIndex(), debug_name);
	mesh->markUploading();

	const vk::DeviceSize vertex_size = data.vertices.size_bytes();
	const vk::DeviceSize index_size = data.indices.size_bytes();
	const vk::DeviceSize skin_vertex_size = data.skin_vertices.size_bytes();
	const vk::DeviceSize total_size = vertex_size + index_size + skin_vertex_size;

	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = total_size;
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
	staging_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
	                 vma::AllocationCreateFlagBits::eHostAccessAllowTransferInstead;

	vertex_staging = core.getAllocator().createBuffer(staging_ci, alloc_ci);
	host_bytes = total_size;
	if (!debug_name.empty()) {
		setDebugName(core, *vertex_staging, std::format("{} StagingBuffer", debug_name));
	}

	const auto& allocation = vertex_staging.getAllocation();
	uint8_t* mapped = static_cast<uint8_t*>(allocation.getInfo().pMappedData);
	if (!mapped) {
		TOAST_CRITICAL("Render", "Unified staging buffer is not mapped");
	}

	std::memcpy(mapped, data.vertices.data(), vertex_size);
	std::memcpy(mapped + vertex_size, data.indices.data(), index_size);
	if (skin_vertex_size > 0) {
		std::memcpy(mapped + vertex_size + index_size, data.skin_vertices.data(), skin_vertex_size);
	}
}

void MeshUpload::record(vk::CommandBuffer cmd) {
	ZoneScoped;
	const vk::DeviceSize vertex_size = mesh->m_vertex_size;
	const vk::DeviceSize index_size = mesh->m_index_size;

	mesh->recordUpload(cmd, *vertex_staging, 0, vertex_size, vertex_size + index_size);

	std::vector<vk::BufferMemoryBarrier> barriers = {
	  vk::BufferMemoryBarrier(
	      vk::AccessFlagBits::eTransferWrite,
	      vk::AccessFlags {},
	      VK_QUEUE_FAMILY_IGNORED,
	      VK_QUEUE_FAMILY_IGNORED,
	      mesh->m_vertex_buffer.value(),
	      0,
	      vertex_size
	  ),
	  vk::BufferMemoryBarrier(
	      vk::AccessFlagBits::eTransferWrite,
	      vk::AccessFlags {},
	      VK_QUEUE_FAMILY_IGNORED,
	      VK_QUEUE_FAMILY_IGNORED,
	      mesh->m_index_buffer.value(),
	      0,
	      mesh->m_index_size
	  )
	};

	if (mesh->m_skin_vertex_buffer.has_value()) {
		barriers.emplace_back(
		    vk::AccessFlagBits::eTransferWrite,
		    vk::AccessFlags {},
		    VK_QUEUE_FAMILY_IGNORED,
		    VK_QUEUE_FAMILY_IGNORED,
		    mesh->m_skin_vertex_buffer.value(),
		    0,
		    mesh->m_skin_vertex_size
		);
	}

	// No BLAS build here since transfer only families cannot build acceleration structures
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, barriers, nullptr
	);
}

}
