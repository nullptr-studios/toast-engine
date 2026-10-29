/**
 * @file lens_flare_pass.hpp
 * @author dario
 * @date 29/10/2026
 */

#pragma once

#include "../post_process_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

class LensFlarePass : public IPostProcessPass {
public:
	LensFlarePass(const VulkanCore& core, vk::Format hdr_format, vk::Extent2D extent);

	[[nodiscard]]
	auto atOutputResolution() const -> bool override {
		return true;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Lens flare";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors lens_flare.slang LensFlareParams
	struct Params {
		glm::vec4 texel {0.0f};
		glm::vec4 flare {0.0f};
		glm::vec4 halo {0.0f};
		glm::vec4 glare {0.0f};
		glm::vec4 tint {1.0f};
	};

	struct Target {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		vk::Extent2D extent;
		sync::ImageState state;
	};

	/// Targets in the order they are drawn into
	enum Step : uint32_t {
		step_threshold,
		step_glare,
		step_blur_down,
		step_blur_up,
		step_chroma,
		step_ghosts,
		step_halo,
		step_mix,
		step_count,
	};

	struct FrameSets {
		std::vector<vk::raii::DescriptorSet> sets;
		vk::ImageView bound_scene = nullptr;
	};

	void createTargets(const VulkanCore& core, vk::Extent2D extent);
	void createPipelines(const VulkanCore& core);

	void writeDescriptor(vk::DescriptorSet set, vk::ImageView source, vk::ImageView scene, vk::ImageView glare);

	void draw(
	    vk::CommandBuffer cmd, const VulkanPipeline& pipeline, vk::DescriptorSet set, Target& target, const Params& params,
	    vk::AttachmentLoadOp load, uint32_t vertex_count, uint32_t instance_count
	);

	const VulkanCore* m_core = nullptr;
	vk::Format m_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_threshold_pipeline;
	VulkanPipeline m_glare_pipeline;
	VulkanPipeline m_blur_down_pipeline;
	VulkanPipeline m_blur_up_pipeline;
	VulkanPipeline m_chroma_pipeline;
	VulkanPipeline m_ghosts_pipeline;
	VulkanPipeline m_halo_pipeline;
	VulkanPipeline m_mix_pipeline;

	vk::raii::Sampler m_sampler = nullptr;

	Target m_quarter_a;
	Target m_quarter_b;
	Target m_flares;
	Target m_glare_target;
	Target m_eighth;
	Target m_composite;

	std::vector<FrameSets> m_frame_sets;
};

}
