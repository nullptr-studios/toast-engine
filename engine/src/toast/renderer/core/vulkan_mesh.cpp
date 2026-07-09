/// @file VulkanMesh.cpp
/// @author dario
/// @date 07/06/2026.

#include "vulkan_mesh.hpp"

#include "toast/log.hpp"
#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <format>
#include <type_traits>

namespace toast::renderer {
static_assert(std::is_standard_layout_v<Vertex>, "Vertex must be standard layout");
static_assert(sizeof(Vertex) == 60, "Vertex size must match mesh.slang input layout (60 bytes)");
static_assert(offsetof(Vertex, position) == 0, "Vertex.position offset mismatch");
static_assert(offsetof(Vertex, normal) == 12, "Vertex.normal offset mismatch");
static_assert(offsetof(Vertex, uv) == 24, "Vertex.uv offset mismatch");
static_assert(offsetof(Vertex, tangent) == 32, "Vertex.tangent offset mismatch");
static_assert(offsetof(Vertex, color) == 48, "Vertex.color offset mismatch");

vk::VertexInputBindingDescription Vertex::getBindingDescription() {
	return vk::VertexInputBindingDescription(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
}

std::array<vk::VertexInputAttributeDescription, 5> Vertex::getAttributeDescriptions() {
	return {
	  vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)),
	  vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)),
	  vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)),
	  vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, tangent)),
	  vk::VertexInputAttributeDescription(4, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color))
	};
}

void VulkanMesh::create(
    const toast::renderer::VulkanCore& core, UploadData data, uint32_t graphicsQueueFamilyIndex,
    uint32_t transferQueueFamilyIndex, std::string_view debug_name
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

	m_vertexCount = static_cast<uint32_t>(data.vertices.size());
	m_indexCount = static_cast<uint32_t>(data.indices.size());
	m_vertexSize = data.vertices.size_bytes();
	m_indexSize = data.indices.size_bytes();

	const bool useConcurrentSharing = graphicsQueueFamilyIndex != transferQueueFamilyIndex;

	// Vertex buffer
	vk::BufferCreateInfo vbCI {};
	vbCI.size = m_vertexSize;
	vbCI.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer;

	std::array familyIndices {graphicsQueueFamilyIndex, transferQueueFamilyIndex};

	if (useConcurrentSharing) {
		vbCI.sharingMode = vk::SharingMode::eConcurrent;
		vbCI.queueFamilyIndexCount = 2;
		vbCI.pQueueFamilyIndices = familyIndices.data();
	} else {
		vbCI.sharingMode = vk::SharingMode::eExclusive;
	}

	vma::AllocationCreateInfo vbAlloc {};
	vbAlloc.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_vertexBuffer.emplace(core.getAllocator().createBuffer(vbCI, vbAlloc));
	if (!debug_name.empty()) {
		setDebugName(core, **m_vertexBuffer, std::format("{} VertexBuffer", debug_name));
	}

	// Index buffer
	vk::BufferCreateInfo ibCI {};
	ibCI.size = m_indexSize;
	ibCI.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer;

	if (useConcurrentSharing) {
		ibCI.sharingMode = vk::SharingMode::eConcurrent;
		ibCI.queueFamilyIndexCount = 2;
		ibCI.pQueueFamilyIndices = familyIndices.data();
	} else {
		ibCI.sharingMode = vk::SharingMode::eExclusive;
	}

	vma::AllocationCreateInfo ibAlloc {};
	ibAlloc.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_indexBuffer.emplace(core.getAllocator().createBuffer(ibCI, ibAlloc));
	if (!debug_name.empty()) {
		setDebugName(core, **m_indexBuffer, std::format("{} IndexBuffer", debug_name));
	}
}

void VulkanMesh::destroy() {
	m_vertexBuffer.reset();
	m_indexBuffer.reset();

	m_vertexCount = 0;
	m_indexCount = 0;

	m_vertexSize = 0;
	m_indexSize = 0;
}

void VulkanMesh::recordUpload(
    vk::CommandBuffer cmd, vk::Buffer stagingBuffer, vk::DeviceSize vertexOffset, vk::DeviceSize indexOffset
) const {
	if (!m_vertexBuffer || !m_indexBuffer) {
		TOAST_CRITICAL("VulkanMesh", "Mesh buffers were not created before upload");
	}

	// Copy using the explicit offsets out of the single staging buffer
	cmd.copyBuffer(stagingBuffer, **m_vertexBuffer, vk::BufferCopy(vertexOffset, 0, m_vertexSize));
	cmd.copyBuffer(stagingBuffer, **m_indexBuffer, vk::BufferCopy(indexOffset, 0, m_indexSize));
}

void VulkanMesh::bind(vk::CommandBuffer cmd) const {
	cmd.bindVertexBuffers(0, {*m_vertexBuffer}, {0});

	cmd.bindIndexBuffer(*m_indexBuffer, 0, vk::IndexType::eUint32);
}

void VulkanMesh::draw(vk::CommandBuffer cmd) const {
	if (!isReady()) {
		return;
	}

	cmd.drawIndexed(m_indexCount, 1, 0, 0, 0);
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

	const vk::DeviceSize vertexSize = data.vertices.size_bytes();
	const vk::DeviceSize indexSize = data.indices.size_bytes();
	const vk::DeviceSize totalSize = vertexSize + indexSize;

	// Single unified staging buffer
	vk::BufferCreateInfo stagingCI {};
	stagingCI.size = totalSize;
	stagingCI.usage = vk::BufferUsageFlagBits::eTransferSrc;
	stagingCI.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo allocCI {};
	allocCI.usage = vma::MemoryUsage::eAuto;
	allocCI.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
	                vma::AllocationCreateFlagBits::eHostAccessAllowTransferInstead;

	vertexStaging = core.getAllocator().createBuffer(stagingCI, allocCI);
	if (!debug_name.empty()) {
		setDebugName(core, *vertexStaging, std::format("{} StagingBuffer", debug_name));
	}

	auto& allocation = vertexStaging.getAllocation();
	uint8_t* mapped = static_cast<uint8_t*>(allocation.getInfo().pMappedData);
	if (!mapped) {
		TOAST_CRITICAL("MeshUpload", "Unified staging buffer is not mapped");
	}

	// Sequential writes to contiguous memory blocks
	std::memcpy(mapped, data.vertices.data(), vertexSize);
	std::memcpy(mapped + vertexSize, data.indices.data(), indexSize);
}

void MeshUpload::record(vk::CommandBuffer cmd) {
	const vk::DeviceSize vertexSize = mesh->m_vertexSize;

	// Record using a single buffer with an offset for the indices
	mesh->recordUpload(cmd, *vertexStaging, 0, vertexSize);

	std::array<vk::BufferMemoryBarrier, 2> barriers = {
	  vk::BufferMemoryBarrier(
	      vk::AccessFlagBits::eTransferWrite,
	      vk::AccessFlags {},
	      VK_QUEUE_FAMILY_IGNORED,
	      VK_QUEUE_FAMILY_IGNORED,
	      mesh->m_vertexBuffer.value(),
	      0,
	      vertexSize
	  ),
	  vk::BufferMemoryBarrier(
	      vk::AccessFlagBits::eTransferWrite,
	      vk::AccessFlags {},
	      VK_QUEUE_FAMILY_IGNORED,
	      VK_QUEUE_FAMILY_IGNORED,
	      mesh->m_indexBuffer.value(),
	      0,
	      mesh->m_indexSize
	  )
	};

	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, barriers, nullptr
	);
}

}
