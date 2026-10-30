/**
 * @file motion_blur_pass.hpp
 * @author dario
 * @date 29/10/2026
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

class MotionBlurPass : public IPostProcessPass {
public:
	MotionBlurPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto atOutputResolution() const -> bool override {
		return true;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Motion blur";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors motion_blur.slang MotionBlurParams
	struct Params {
		glm::mat4 reprojection {1.0f};
		glm::vec4 texel {0.0f};
		glm::vec4 tuning {0.0f};
		glm::vec4 camera {0.0f};
	};

	void createResources(const VulkanCore& core);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	vk::raii::Sampler m_linear_sampler = nullptr;
	vk::raii::Sampler m_point_sampler = nullptr;

	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<vk::ImageView> m_bound_views;

	PostProcessTarget m_target;

	uint64_t m_sequence = 0;
	bool m_have_sequence = false;
	glm::mat4 m_view_projection {1.0f};
	glm::mat4 m_previous_view_projection {1.0f};
};

}
