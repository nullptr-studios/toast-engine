/**
 * @file ssr_pass.hpp
 * @author dario
 * @date 07/08/2026
 */

#pragma once

#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/passes/post_process_pass_base.hpp"
#include "toast/renderer/resources/post_process_target.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

/// Half resolution stochastic SSR
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
		/// x reach in meters y render mode z 1 with a valid history w 1 when binding 8 holds last frame resolve
		glm::vec4 marching {0.0f};
	};

	/// xy hit uv z pdf w confidence in 32 bit since 16 bit cannot address wide screens
	static constexpr vk::Format k_hit_format = vk::Format::eR32G32B32A32Sfloat;
	/// rgb reflection a confidence
	static constexpr vk::Format k_reflection_format = vk::Format::eR16G16B16A16Sfloat;

	/// Bindings of ssr.slang in order
	static constexpr uint32_t k_binding_count = 9;

	void createResources(const VulkanCore& core);
	void createTargets(const VulkanCore& core, vk::Extent2D extent);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_trace_pipeline;
	VulkanPipeline m_resolve_pipeline;
	VulkanPipeline m_temporal_pipeline;
	VulkanPipeline m_composite_pipeline;

	vk::raii::Sampler m_sampler = nullptr;
	vk::raii::Sampler m_point_sampler = nullptr;

	/// Per frame in flight then per history parity
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	/// Source then previous resolve last written into each set
	std::vector<std::array<vk::ImageView, 4>> m_bound_views;

	PostProcessTarget m_hit_target;
	PostProcessTarget m_resolve_target;
	/// The temporal pass reads one and writes the other
	std::array<PostProcessTarget, 2> m_history;
	PostProcessTarget m_target;

	/// Which history the temporal pass writes this frame
	uint32_t m_parity = 0;
	bool m_history_valid = false;
	/// A gap in frames or a recreated target restarts the history
	uint64_t m_last_sequence = 0;
};

}
