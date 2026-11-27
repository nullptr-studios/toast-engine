/**
 * @file taa_pass.hpp
 * @author dario
 * @date 25/09/2026
 */

#pragma once

#include "toast/renderer/core/descriptor_cache.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/passes/post/motion_fill.hpp"
#include "toast/renderer/passes/post_process_pass_base.hpp"
#include "toast/renderer/resources/post_process_target.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"
#include "toast/renderer/upscale/dlss_upscaler.hpp"

#include <array>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace renderer {
class VulkanCore;

class TaaPass : public IPostProcessPass {
public:
	/// Mirrors the editor RenderMode.MotionVectors index
	static constexpr uint32_t k_render_mode_motion = 26;

	TaaPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto atOutputResolution() const -> bool override {
		return true;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "TAA";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

	/// Last frame resolve or null without one
	[[nodiscard]]
	auto resolvedHistory() const noexcept -> vk::ImageView {
		return m_history_valid ? m_targets[m_write ^ 1u].view() : vk::ImageView {};
	}

	/// Empty when DLSS is available
	[[nodiscard]]
	auto dlssFailureReason() const -> std::string {
		return m_dlss.failureReason();
	}

private:
	/// Mirrors taa.slang TaaParams
	struct Params {
		glm::vec4 texel {0.0f};
		glm::vec4 tuning {0.0f};
		/// xy render size zw jitter in uv
		glm::vec4 source {0.0f};
		/// x output pixels of motion at which the history weight halves
		glm::vec4 adapt {0.0f};
	};

	void createResources(const VulkanCore& core);
	void createTargets(const VulkanCore& core, vk::Extent2D extent);
	/// Resolves into @p target with DLSS, false when the caller must run the TAA draw instead
	auto recordDlss(
	    vk::CommandBuffer cmd, vk::ImageView source_view, const PostProcessTarget& target, glm::vec2 jitter_pixels, bool reset
	) -> bool;

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;

	DlssUpscaler m_dlss;
	MotionFill m_motion_fill;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	vk::raii::Sampler m_linear_sampler = nullptr;
	vk::raii::Sampler m_point_sampler = nullptr;

	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	/// Source, history and filled motion last written into each set
	DescriptorCache<std::array<vk::ImageView, 3>> m_bound_views;

	/// Last frame output is this frame history
	std::array<PostProcessTarget, 2> m_targets;
	uint32_t m_write = 0;

	bool m_history_valid = false;
	bool m_dlss_source_logged = false;
	uint64_t m_last_sequence = 0;
	glm::mat4 m_last_view_projection {1.0f};
};

}
