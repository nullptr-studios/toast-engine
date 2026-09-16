/**
 * @file ssr_pass.hpp
 * @author dario
 * @date 07/08/2026
 */

#pragma once

#include "../post_process_pass_base.hpp"
#include "../post_process_target.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

class SsrPass : public IPostProcessPass {
public:
	SsrPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "SSR";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors ssr.slang SsrParams. 176 bytes so the constructor checks maxPushConstantsSize
	struct Params {
		glm::mat4 view_projection {1.0f};
		glm::mat4 inverse_view_projection {1.0f};
		glm::vec4 camera_position {0.0f};
		/// x intensity y max roughness z thickness w step count
		glm::vec4 tuning {0.0f};
		/// x stride y render mode
		glm::vec4 marching {0.0f};
	};

	/// rgb reflection a camera distance
	static constexpr vk::Format k_reflection_format = vk::Format::eR16G16B16A16Sfloat;

	void createResources(const VulkanCore& core);
	void createTargets(const VulkanCore& core, vk::Extent2D extent);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_trace_pipeline;
	VulkanPipeline m_composite_pipeline;

	vk::raii::Sampler m_sampler = nullptr;
	vk::raii::Sampler m_point_sampler = nullptr;

	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<vk::ImageView> m_bound_views;

	PostProcessTarget m_reflection_target;
	PostProcessTarget m_target;
};

}
