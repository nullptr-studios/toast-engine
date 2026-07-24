/**
 * @file world_ui_pass.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Draws Panel3D output textures as depth-tested quads in world space
 */

#pragma once
#include <array>
#include <glm/glm.hpp>
#include <toast/renderer/render_pass_base.hpp>
#include <toast/renderer/shader_layout.hpp>
#include <toast/renderer/vulkan_common.hpp>
#include <toast/renderer/vulkan_pipeline.hpp>
#include <vector>

namespace renderer {
class VulkanCore;
}

namespace ui {

class WorldUIPass : public IRenderPass {
public:
	WorldUIPass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent);

	void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

private:
	struct PushConstants {
		glm::mat4 model;
	};

	const renderer::VulkanCore* m_core = nullptr;

	vk::raii::Sampler m_sampler = nullptr;
	renderer::ShaderLayout m_shader_layout;
	renderer::VulkanPipeline m_pipeline;

	std::vector<vk::raii::DescriptorSet> m_frame_camera_sets;

	std::array<std::vector<vk::raii::DescriptorSet>, 3> m_panel_sets;
	uint32_t m_draw_count = 0;
};

}
