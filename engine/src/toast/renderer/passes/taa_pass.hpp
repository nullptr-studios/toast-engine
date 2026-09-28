/**
 * @file taa_pass.hpp
 * @author dario
 * @date 25/09/2026
 */

#pragma once

#include "../post_process_pass_base.hpp"
#include "../post_process_target.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace renderer {
class VulkanCore;

class TaaPass : public IPostProcessPass {
public:
	/// Mirrors the editor RenderMode.MotionVectors index
	static constexpr uint32_t k_render_mode_motion = 25;

	TaaPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "TAA";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors taa.slang TaaParams
	struct Params {
		glm::mat4 reprojection {1.0f};
		glm::vec4 texel {0.0f};
		glm::vec4 tuning {0.0f};
	};

	void createResources(const VulkanCore& core);
	void createTargets(const VulkanCore& core, vk::Extent2D extent);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	vk::raii::Sampler m_linear_sampler = nullptr;
	vk::raii::Sampler m_point_sampler = nullptr;

	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	/// Source then history last written into each set
	std::vector<std::array<vk::ImageView, 2>> m_bound_views;

	/// Last frame output is this frame history
	std::array<PostProcessTarget, 2> m_targets;
	uint32_t m_write = 0;

	bool m_history_valid = false;
	uint64_t m_last_sequence = 0;
	glm::mat4 m_last_view_projection {1.0f};
};

}
