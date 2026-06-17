/// @file DebugPass.hpp
/// @author dario
/// @date 10/06/2026.

#pragma once
#include "core/IRenderPass.hpp"
#include "core/ShaderLayout.hpp"
#include "core/VulkanPipeline.hpp"
#include "core/vulkan_common.hpp"

namespace toast::renderer {
class VulkanCore;

class DebugPass : public IRenderPass {
public:
	DebugPass(const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent);

	void record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) override;

	void update(uint32_t frame_index, float dt) override;

private:
	void CreateResources(const toast::renderer::VulkanCore& core);

	VulkanPipeline m_line_pipeline;
	ShaderLayout m_line_shader_layout;
	VulkanPipeline m_gizmo_pipeline;
	ShaderLayout m_gizmo_shader_layout;
	VulkanPipeline m_plane_pipeline;
	ShaderLayout m_plane_shader_layout;
};

}
