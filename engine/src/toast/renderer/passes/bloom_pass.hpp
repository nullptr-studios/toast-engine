/**
 * @file bloom_pass.hpp
 * @author dario
 * @date 03/08/2026
 */

#pragma once

#include "../post_process_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

class BloomPass : public IPostProcessPass {
public:
	BloomPass(const VulkanCore& core, vk::Format hdr_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Bloom";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors bloom.slang BloomParams
	struct Params {
		glm::vec2 inverse_source_size {0.0f};
		float threshold = 1.0f;
		float knee = 0.5f;
		float filter_radius = 1.0f;
		float strength = 0.05f;
		glm::vec2 pad0 {0.0f};
	};

	struct Mip {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		vk::Extent2D extent;
		sync::ImageState state;
	};

	void createTargets(const VulkanCore& core, vk::Extent2D extent);
	void createPipelines(const VulkanCore& core);

	void writeDescriptor(vk::DescriptorSet set, vk::ImageView source, vk::ImageView scene);

	void drawInto(
	    vk::CommandBuffer cmd, const VulkanPipeline& pipeline, vk::DescriptorSet set, const Mip& target, const Params& params,
	    bool additive
	);

	const VulkanCore* m_core = nullptr;
	vk::Format m_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_downsample_first_pipeline;
	VulkanPipeline m_downsample_pipeline;
	VulkanPipeline m_upsample_pipeline;
	VulkanPipeline m_composite_pipeline;

	vk::raii::Sampler m_sampler = nullptr;

	std::vector<Mip> m_mips;

	Mip m_composite;

	/// Per frame in flight since the TAA ping pong source changes and a pending frame still binds the old sets
	struct FrameSets {
		std::vector<vk::raii::DescriptorSet> downsample;
		std::vector<vk::raii::DescriptorSet> upsample;
		vk::raii::DescriptorSet composite = nullptr;
		vk::ImageView bound_source = nullptr;
	};

	std::vector<FrameSets> m_frame_sets;
};

}
