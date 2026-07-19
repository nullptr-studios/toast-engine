/**
 * @file grid_pass.hpp
 * @author Xein
 * @date 17 Jul 2026
 */

#pragma once
#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_pipeline.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace renderer {
class VulkanCore;

/**
 * @brief Editor ground grid pass
 */
class GridPass : public IRenderPass {
public:
	GridPass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent);

	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Grid";
	}

private:
	struct DrawPushConstants {
		glm::mat4 model;
	};

	void createResources(const renderer::VulkanCore& core);

	VulkanPipeline m_pipeline;
	ShaderLayout m_shader_layout;
	std::vector<vk::raii::DescriptorSet> m_frame_descriptor_sets;
	vma::raii::Buffer m_vertex_buffer = nullptr;
};

}
