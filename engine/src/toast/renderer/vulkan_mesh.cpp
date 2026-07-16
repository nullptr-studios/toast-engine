/// @file VulkanMesh.cpp
/// @author dario
/// @date 07/06/2026.

#include "vulkan_mesh.hpp"

#include "toast/log.hpp"
#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <format>
#include <type_traits>

namespace renderer {
static_assert(std::is_standard_layout_v<Vertex>, "Vertex must be standard layout");
static_assert(sizeof(Vertex) == 60, "Vertex size must match mesh.slang input layout (60 bytes)");
static_assert(offsetof(Vertex, position) == 0, "Vertex.position offset mismatch");
static_assert(offsetof(Vertex, normal) == 12, "Vertex.normal offset mismatch");
static_assert(offsetof(Vertex, uv) == 24, "Vertex.uv offset mismatch");
static_assert(offsetof(Vertex, tangent) == 32, "Vertex.tangent offset mismatch");
static_assert(offsetof(Vertex, color) == 48, "Vertex.color offset mismatch");

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
	if (data.vertices.empty()) {
		TOAST_CRITICAL("VulkanMesh", "Mesh has no vertices");
	}

	if (data.indices.empty()) {
		TOAST_CRITICAL("VulkanMesh", "Mesh has no indices");
	}

	if (isReady()) {
		destroy();
	}

	m_vertex_count = static_cast<uint32_t>(data.vertices.size());
	m_index_count = static_cast<uint32_t>(data.indices.size());
	m_vertex_size = data.vertices.size_bytes();
	m_index_size = data.indices.size_bytes();

	const bool use_concurrent_sharing = graphics_queue_family_index != transfer_queue_family_index;

	// Vertex buffer
	vk::BufferCreateInfo vb_ci {};
	vb_ci.size = m_vertex_size;
	vb_ci.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer;

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

	// Index buffer
	vk::BufferCreateInfo ib_ci {};
	ib_ci.size = m_index_size;
	ib_ci.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer;

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
}

void VulkanMesh::destroy() {
	m_vertex_buffer.reset();
	m_index_buffer.reset();

	m_vertex_count = 0;
	m_index_count = 0;

	m_vertex_size = 0;
	m_index_size = 0;
}

void VulkanMesh::recordUpload(
    vk::CommandBuffer cmd, vk::Buffer staging_buffer, vk::DeviceSize vertex_offset, vk::DeviceSize index_offset
) const {
	if (!m_vertex_buffer || !m_index_buffer) {
		TOAST_CRITICAL("VulkanMesh", "Mesh buffers were not created before upload");
	}

	// Copy using the explicit offsets out of the single staging buffer
	cmd.copyBuffer(staging_buffer, **m_vertex_buffer, vk::BufferCopy(vertex_offset, 0, m_vertex_size));
	cmd.copyBuffer(staging_buffer, **m_index_buffer, vk::BufferCopy(index_offset, 0, m_index_size));
}

void VulkanMesh::bind(vk::CommandBuffer cmd) const {
	cmd.bindVertexBuffers(0, {*m_vertex_buffer}, {0});

	cmd.bindIndexBuffer(*m_index_buffer, 0, vk::IndexType::eUint32);
}

void VulkanMesh::draw(vk::CommandBuffer cmd) const {
	if (!isReady()) {
		return;
	}

	cmd.drawIndexed(m_index_count, 1, 0, 0, 0);
}

// MeshUpload

MeshUpload::MeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data, std::string_view debug_name) {
	this->mesh = &mesh;
	this->data = data;
	this->debug_name = debug_name;
}

void MeshUpload::build(const VulkanCore& core) {
	mesh->create(core, data, core.getGraphicsQueueFamilyIndex(), core.getTransferQueueFamilyIndex(), debug_name);
	mesh->markUploading();

	const vk::DeviceSize vertex_size = data.vertices.size_bytes();
	const vk::DeviceSize index_size = data.indices.size_bytes();
	const vk::DeviceSize total_size = vertex_size + index_size;

	// Single unified staging buffer
	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = total_size;
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
	staging_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
	                 vma::AllocationCreateFlagBits::eHostAccessAllowTransferInstead;

	vertex_staging = core.getAllocator().createBuffer(staging_ci, alloc_ci);
	if (!debug_name.empty()) {
		setDebugName(core, *vertex_staging, std::format("{} StagingBuffer", debug_name));
	}

	const auto& allocation = vertex_staging.getAllocation();
	uint8_t* mapped = static_cast<uint8_t*>(allocation.getInfo().pMappedData);
	if (!mapped) {
		TOAST_CRITICAL("MeshUpload", "Unified staging buffer is not mapped");
	}

	// Sequential writes to contiguous memory blocks
	std::memcpy(mapped, data.vertices.data(), vertex_size);
	std::memcpy(mapped + vertex_size, data.indices.data(), index_size);
}

void MeshUpload::record(vk::CommandBuffer cmd) {
	const vk::DeviceSize vertex_size = mesh->m_vertex_size;

	// Record using a single buffer with an offset for the indices
	mesh->recordUpload(cmd, *vertex_staging, 0, vertex_size);

	std::array<vk::BufferMemoryBarrier, 2> barriers = {
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

	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, barriers, nullptr
	);
}

}
