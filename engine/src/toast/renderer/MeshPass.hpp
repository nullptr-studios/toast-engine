/// @file MeshPass.hpp
/// @author dario
/// @date 08/06/2026.

#pragma once
#include "IRenderPass.hpp"
#include "ShaderLayout.hpp"
#include "VulkanMesh.hpp"
#include "VulkanPipeline.hpp"

#include <glm/glm.hpp>

namespace toast::renderer {
class VulkanCore;
}

class MeshPass : public IRenderPass {
public:
	struct CameraUBO {
		glm::mat4 view;
		glm::mat4 projection;
		glm::mat4 viewProjection;
	};

	struct DrawPushConstants {
		glm::mat4 model;
	};

	MeshPass(const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent);

	void update(uint32_t frame_index, float dt) override;

	void record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) override;

private:
	void createResources(const toast::renderer::VulkanCore& core);

	void updateUBO(uint32_t frameIndex);

	toast::renderer::VulkanPipeline m_pipeline;

	vk::raii::DescriptorPool m_descriptorPool = nullptr;

	std::vector<CameraUBO> cameraUBO;
	std::vector<FrameResources> cameraUBORes;

	toast::renderer::ShaderLayout shaderLayout;

	toast::renderer::VulkanMesh mesh;
};
