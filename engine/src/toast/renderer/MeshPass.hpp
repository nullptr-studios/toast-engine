/// @file MeshPass.hpp
/// @author dario
/// @date 08/06/2026.

#pragma once
#include "core/render_pass_base.hpp"
#include "core/shader_layout.hpp"
#include "core/vulkan_mesh.hpp"
#include "core/vulkan_pipeline.hpp"

#include <glm/glm.hpp>

namespace toast::renderer {
class VulkanCore;

/// @brief Render pass for drawing meshes with camera matrices and transformations
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

	toast::renderer::ShaderLayout shaderLayout;
};
}
