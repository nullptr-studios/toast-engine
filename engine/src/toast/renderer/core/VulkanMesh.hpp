/// @file VulkanMesh.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include "glm/glm.hpp"
#include "vulkan_common.hpp"

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

// TODO: IResource interface
enum class ResourceUploadState : uint8_t {
	Uploading,
	Ready
};

/// @brief GPU mesh with vertex and index buffers stored in VRAM
class VulkanMesh {
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

	void markUploading() { m_uploadState = ResourceUploadState::Uploading; }

	void markReady() { m_uploadState = ResourceUploadState::Ready; }

	bool isReady() const { return m_uploadState == ResourceUploadState::Ready; }

private:
	std::optional<vma::raii::Buffer> m_vertexBuffer;
	std::optional<vma::raii::Buffer> m_indexBuffer;

	vk::DeviceSize m_vertexSize = 0;
	vk::DeviceSize m_indexSize = 0;

	uint32_t m_vertexCount = 0;
	uint32_t m_indexCount = 0;

	ResourceUploadState m_uploadState = ResourceUploadState::Uploading;
};
}
