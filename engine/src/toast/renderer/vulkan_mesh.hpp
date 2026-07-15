/// @file VulkanMesh.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include "glm/glm.hpp"
#include "vulkan_common.hpp"
#include "vulkan_resource_base.hpp"

#include <string>
#include <string_view>

namespace toast::renderer {
class VulkanCore;

/// @brief Vertex with position, normals, UVs, tangents, and colors for mesh rendering
struct Vertex {
	glm::vec<3, float, glm::packed_highp> position;
	glm::vec<3, float, glm::packed_highp> normal;
	glm::vec<2, float, glm::packed_highp> uv;
	glm::vec<4, float, glm::packed_highp> tangent;
	glm::vec<3, float, glm::packed_highp> color;

	/*
	JOINTS_0
	WEIGHTS_0
	*/

	static auto getBindingDescription() -> vk::VertexInputBindingDescription;

	static auto getAttributeDescriptions() -> std::array<vk::VertexInputAttributeDescription, 5>;
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
	    const toast::renderer::VulkanCore& core, UploadData data, uint32_t graphics_queue_family_index,
	    uint32_t transfer_queue_family_index, std::string_view debug_name = {}
	);

	void destroy();

	void bind(vk::CommandBuffer cmd) const;
	void draw(vk::CommandBuffer cmd) const;

	void recordUpload(
	    vk::CommandBuffer cmd, vk::Buffer staging_buffer, vk::DeviceSize vertex_offset, vk::DeviceSize index_offset
	) const;

private:
	std::optional<vma::raii::Buffer> m_vertex_buffer;
	std::optional<vma::raii::Buffer> m_index_buffer;

	vk::DeviceSize m_vertex_size = 0;
	vk::DeviceSize m_index_size = 0;

	uint32_t m_vertex_count = 0;
	uint32_t m_index_count = 0;

	friend class MeshUpload;
};

class MeshUpload : public PendingResourceUpload {
public:
	MeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data, std::string_view debug_name = {});

	VulkanMesh* mesh;

	VulkanMesh::UploadData data;
	std::string debug_name;

	vma::raii::Buffer vertex_staging = nullptr;
	vma::raii::Buffer index_staging = nullptr;

	void build(const VulkanCore& core) override;

	void record(vk::CommandBuffer cmd) override;

	auto resource() -> IVulkanResource* override { return mesh; }
};

}
