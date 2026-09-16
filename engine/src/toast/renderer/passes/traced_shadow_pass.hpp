/**
 * @file traced_shadow_pass.hpp
 * @author dario
 * @date 13/08/2026
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

class TracedShadowPass : public IPostProcessPass {
public:
	TracedShadowPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Traced Shadows";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors traced_shadows.slang TracedShadowParams
	struct Params {
		glm::mat4 inverse_view_projection {1.0f};
		glm::vec4 light_direction {0.0f};
		/// x render mode y normal bias z max trace distance
		glm::vec4 tuning {0.0f};
	};

	void createResources(const VulkanCore& core);
	void createTarget(const VulkanCore& core, vk::Extent2D extent);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	vk::raii::Sampler m_sampler = nullptr;
	vk::raii::Sampler m_point_sampler = nullptr;

	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<vk::ImageView> m_bound_views;
	std::vector<vk::AccelerationStructureKHR> m_bound_acceleration_structures;

	PostProcessTarget m_target;
};

}
