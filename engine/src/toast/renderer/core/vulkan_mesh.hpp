/// @file VulkanMesh.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include "glm/glm.hpp"
#include "vulkan_common.hpp"
#include "vulkan_resource_base.hpp"

namespace toast::renderer {
class VulkanCore;

/// @brief Vertex with position, normals, UVs, tangents, and colors for mesh rendering
struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 tangent;
	glm::vec3 color;

	/*
	JOINTS_0
	WEIGHTS_0
	*/

	static vk::VertexInputBindingDescription getBindingDescription();

	static std::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions();
};

/// @brief GPU mesh with vertex and index buffers stored in VRAM
class VulkanMesh : public IVulkanResource {
public:
	VulkanMesh() = default;

	struct UploadData {
		std::span<const Vertex> vertices;
		std::span<const uint32_t> indices;
	};

	void create(
	    const toast::renderer::VulkanCore& core, UploadData data, uint32_t graphicsQueueFamilyIndex,
	    uint32_t transferQueueFamilyIndex
	);

	void destroy();

	void bind(vk::CommandBuffer cmd) const;
	void draw(vk::CommandBuffer cmd) const;

	void recordUpload(vk::CommandBuffer cmd, vk::Buffer stagingVB, vk::Buffer stagingIB) const;

private:
	std::optional<vma::raii::Buffer> m_vertexBuffer;
	std::optional<vma::raii::Buffer> m_indexBuffer;

	vk::DeviceSize m_vertexSize = 0;
	vk::DeviceSize m_indexSize = 0;

	uint32_t m_vertexCount = 0;
	uint32_t m_indexCount = 0;

	friend class MeshUpload;
};

class MeshUpload : public PendingResourceUpload {
public:
	MeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data);

	VulkanMesh* mesh;

	VulkanMesh::UploadData data;

	vma::raii::Buffer vertexStaging = nullptr;
	vma::raii::Buffer indexStaging = nullptr;

	void build(const VulkanCore& core) override;

	void record(vk::CommandBuffer cmd) override;

	IVulkanResource* resource() override { return mesh; }
};

}
