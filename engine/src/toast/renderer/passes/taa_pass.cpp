/**
 * @file taa_pass.cpp
 * @author dario
 * @date 25/09/2026
 */

#include "taa_pass.hpp"

#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

TaaPass::TaaPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format) {
	ZoneScoped;
	static_assert(sizeof(Params) <= 128, "Vulkan guarantees 128 bytes of push constants");

	const auto uid = assets::resolveURI("core://shaders/taa.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "TaaPass shader core://shaders/taa.slang unavailable; temporal anti-aliasing is disabled");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "TaaPass");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "TaaPass";
	config.color_format = scene_format;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	m_pipeline.rebuild(core, config);

	createResources(core);
	createTargets(core, extent);
	TOAST_INFO("Render", "TaaPass ready ({})", vk::to_string(scene_format));
}

void TaaPass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "TaaPass shader layout has no descriptor sets");
		return;
	}

	m_linear_sampler = vk::raii::Sampler(device, linearClampSamplerInfo());
	setDebugName(core, *m_linear_sampler, "TaaPass Sampler");

	// Nearest since texel-exact reads must not blend across edges
	m_point_sampler = vk::raii::Sampler(device, nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "TaaPass PointSampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_descriptor_sets.clear();
	m_bound_views.assign(VulkanRenderer::k_frames_in_flight, {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("TaaPass DescriptorSet[{}]", i));
	}
}

void TaaPass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	m_targets[0].create(core, extent, m_scene_format, "TaaPass History A");
	m_targets[1].create(core, extent, m_scene_format, "TaaPass History B");
	std::ranges::fill(m_bound_views, std::array<vk::ImageView, 2> {});
	m_history_valid = false;
}

void TaaPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr) {
		createTargets(*m_core, extent);
	}
}

auto TaaPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const bool show_motion = frame != nullptr && frame->render_mode == k_render_mode_motion;
	const bool active = frame != nullptr && frame->taa_active;
	if (!active && !show_motion) {
		m_history_valid = false;
		return source_view;
	}

	const vk::ImageView depth_view = VulkanRenderer::instance->getDepthView();
	const vk::ImageView motion_view = VulkanRenderer::instance->getSceneMotionView();
	if (!m_pipeline.isReady() || frame_index >= m_descriptor_sets.size() || !source_view || !depth_view || !motion_view ||
	    !m_targets[0].isReady() || !m_targets[1].isReady()) {
		m_history_valid = false;
		return source_view;
	}

	// A repeated snapshot carries no new sample
	if (active && m_history_valid && frame->sequence == m_last_sequence) {
		return m_targets[m_write ^ 1u].view();
	}

	const PostProcessTarget& target = m_targets[m_write];
	const PostProcessTarget& history = m_targets[m_write ^ 1u];
	const bool reset = !m_history_valid || frame->taa_reset;
	if (reset) {
		// Only for a legal layout since a reset frame never reads the history
		history.beginScope(cmd);
		history.endScope(cmd);
	}

	const std::array<vk::ImageView, 2> wanted {source_view, history.view()};
	if (m_bound_views[frame_index] != wanted) {
		const std::array image_infos {
		  vk::DescriptorImageInfo(*m_point_sampler, source_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_linear_sampler, history.view(), vk::ImageLayout::eShaderReadOnlyOptimal),
		  // Depth stays eDepthReadOnlyOptimal for the overlay stage
		  vk::DescriptorImageInfo(*m_point_sampler, depth_view, vk::ImageLayout::eDepthReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, motion_view, vk::ImageLayout::eShaderReadOnlyOptimal)
		};

		std::array<vk::WriteDescriptorSet, 4> writes {};
		for (uint32_t binding = 0; binding < writes.size(); ++binding) {
			writes[binding] = vk::WriteDescriptorSet(
			    *m_descriptor_sets[frame_index], binding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_infos[binding]
			);
		}
		m_core->getDevice().updateDescriptorSets(writes, {});
		m_bound_views[frame_index] = wanted;
	}

	const glm::mat4 current_view_projection = frame->frame_data.view_projection;
	const glm::mat4 previous_view_projection = m_history_valid ? m_last_view_projection : current_view_projection;

	// A skipped frame breaks written motion so only depth reprojection is trusted
	const bool frames_skipped = m_history_valid && frame->sequence != m_last_sequence + 1;

	const auto width = static_cast<float>(target.extent().width);
	const auto height = static_cast<float>(target.extent().height);

	Params params {};
	params.reprojection = previous_view_projection * glm::inverse(current_view_projection);
	params.texel = glm::vec4(1.0f / width, 1.0f / height, width, height);
	params.tuning = glm::vec4(
	    VulkanRenderer::instance->taaHistoryWeight(), reset ? 1.0f : 0.0f, frames_skipped ? 1.0f : 0.0f, show_motion ? 1.0f : 0.0f
	);

	target.beginScope(cmd);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_descriptor_sets[frame_index]},
	    {}
	);
	cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
	cmd.draw(3, 1, 0, 0);
	target.endScope(cmd);

	const vk::ImageView output = target.view();
	m_write ^= 1u;

	// The motion view is not a resolve and must never become history
	m_history_valid = !show_motion;
	m_last_sequence = frame->sequence;
	m_last_view_projection = current_view_projection;
	return output;
}

}
