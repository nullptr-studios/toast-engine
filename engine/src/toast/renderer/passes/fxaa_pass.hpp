/**
 * @file fxaa_pass.hpp
 * @author dario
 * @date 03/08/2026
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

class FxaaPass : public IPostProcessPass {
public:
	FxaaPass(const VulkanCore& core, vk::Format ldr_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "FXAA";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors fxaa.slang FxaaParams
	struct Params {
		glm::vec2 inverse_source_size {0.0f};
		float contrast_threshold = 0.0312f;
		float relative_threshold = 0.125f;
		float subpixel_blending = 0.75f;
		glm::vec2 pad0 {0.0f};
	};

	void createResources(const VulkanCore& core);
	void createTarget(const VulkanCore& core, vk::Extent2D extent);

	const VulkanCore* m_core = nullptr;
	vk::Format m_ldr_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	vk::raii::Sampler m_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<vk::ImageView> m_bound_views;

	PostProcessTarget m_target;
};

}
