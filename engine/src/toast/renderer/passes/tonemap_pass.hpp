/**
 * @file tonemap_pass.hpp
 * @author dario
 * @date 03/08/2026
 */

#pragma once

#include "../post_process_pass_base.hpp"
#include "../post_process_target.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

enum class TonemapMode : uint8_t {
	reinhard = 0,
	aces = 1,
};

class TonemapPass : public IPostProcessPass {
public:
	TonemapPass(const VulkanCore& core, vk::Format ldr_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Tonemap";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

private:
	/// Mirrors tonemap.slang TonemapParams. std140 pads the block to 48 bytes
	struct Params {
		float exposure = 1.0f;
		uint32_t mode = static_cast<uint32_t>(TonemapMode::reinhard);
		float gamma = 2.2f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float vignette = 0.0f;
		float grain = 0.0f;
		float time = 0.0f;
		glm::vec4 pad0 {0.0f};
	};

	void createResources(const VulkanCore& core);
	void createTarget(const VulkanCore& core, vk::Extent2D extent);

	const VulkanCore* m_core = nullptr;
	vk::Format m_ldr_format = vk::Format::eUndefined;

	PostProcessTarget m_target;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	vk::raii::Sampler m_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<FrameResources> m_params_buffers;

	std::vector<vk::ImageView> m_bound_views;
};

}
