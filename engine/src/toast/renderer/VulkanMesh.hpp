/// @file VulkanMesh.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include "glm/glm.hpp"
#include "vulkan_common.hpp"

namespace toast::renderer {
class VulkanCore;

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 tangent;

	/*
	COLOR_0
	JOINTS_0
	WEIGHTS_0
	*/

	static vk::VertexInputBindingDescription getBindingDescription();

	static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions();
};

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

	[[nodiscard]]
	bool isReady() const {
		return m_vertexBuffer.has_value() && m_indexBuffer.has_value() && m_vertexCount > 0 && m_indexCount > 0;
	}

private:
	std::optional<vma::raii::Buffer> m_vertexBuffer;
	std::optional<vma::raii::Buffer> m_indexBuffer;

	vk::DeviceSize m_vertexSize = 0;
	vk::DeviceSize m_indexSize = 0;

	uint32_t m_vertexCount = 0;
	uint32_t m_indexCount = 0;
};
}
