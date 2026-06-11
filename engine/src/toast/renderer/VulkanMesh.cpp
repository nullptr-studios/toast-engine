/// @file VulkanMesh.cpp
/// @author dario
/// @date 07/06/2026.

#include "VulkanMesh.hpp"

#include "VulkanCore.hpp"
#include "toast/log.hpp"

namespace toast::renderer {
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
    const toast::renderer::VulkanCore& core, UploadData data, uint32_t graphicsQueueFamilyIndex, uint32_t transferQueueFamilyIndex
) {
	if (data.vertices.empty()) {
		TOAST_CRITICAL("VulkanMesh", "Mesh has no vertices");
	}

	if (data.indices.empty()) {
		TOAST_CRITICAL("VulkanMesh", "Mesh has no indices");
	}

	destroy();

	m_vertexCount = static_cast<uint32_t>(data.vertices.size());
	m_indexCount = static_cast<uint32_t>(data.indices.size());
	m_vertexSize = data.vertices.size_bytes();
	m_indexSize = data.indices.size_bytes();

	const bool useConcurrentSharing = graphicsQueueFamilyIndex != transferQueueFamilyIndex;

	// Vertex buffer
	vk::BufferCreateInfo vbCI {};
	vbCI.size = m_vertexSize;
	vbCI.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer;

	std::array<uint32_t, 2> familyIndices {graphicsQueueFamilyIndex, transferQueueFamilyIndex};

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
}

void VulkanMesh::destroy() { }

void VulkanMesh::recordUpload(vk::CommandBuffer cmd, vk::Buffer stagingVB, vk::Buffer stagingIB) const {
	if (!m_vertexBuffer || !m_indexBuffer) {
		TOAST_CRITICAL("VulkanMesh", "Mesh buffers were not created before upload");
	}

	cmd.copyBuffer(stagingVB, **m_vertexBuffer, vk::BufferCopy(0, 0, m_vertexSize));

	cmd.copyBuffer(stagingIB, **m_indexBuffer, vk::BufferCopy(0, 0, m_indexSize));
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
}
