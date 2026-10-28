/**
 * @file ssr_pass.cpp
 * @author dario
 * @date 07/08/2026
 */

#include "ssr_pass.hpp"

#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <glm/gtc/matrix_inverse.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

SsrPass::SsrPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format) {
	ZoneScoped;
	// Vulkan guarantees 128 bytes and this block is 176
	const uint32_t push_constant_limit = core.getPhysicalDevice().getProperties().limits.maxPushConstantsSize;
	if (push_constant_limit < sizeof(Params)) {
		TOAST_ERROR(
		    "Render",
		    "SsrPass needs {} bytes of push constants but this device allows {}; screen-space reflections are disabled",
		    sizeof(Params),
		    push_constant_limit
		);
		setEnabled(false);
		return;
	}

	const auto uid = assets::resolveURI("core://shaders/ssr.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "SsrPass shader core://shaders/ssr.slang unavailable; screen-space reflections are disabled");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "SsrPass");

	const auto build_pipeline =
	    [&](VulkanPipeline& pipeline, vk::Format format, vk::Extent2D target_extent, const char* entry, const char* debug_name) {
		    VulkanPipeline::Config config;
		    config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		    config.debug_name = debug_name;
		    config.color_format = format;
		    // Confidence lives in alpha
		    config.write_color_alpha = true;
		    config.extent = target_extent;
		    config.shader_spirv = shader->spirv;
		    config.fragment_entry = entry;
		    config.pipeline_layout = *m_shader_layout.getPipelineLayout();
		    config.vertex_bindings = {};
		    config.vertex_attributes = {};
		    config.cull_mode = vk::CullModeFlagBits::eNone;
		    config.depth_test = false;
		    config.depth_write = false;
		    pipeline.rebuild(core, config);
	    };
	const vk::Extent2D half = PostProcessTarget::halfExtent(extent);
	build_pipeline(m_trace_pipeline, k_hit_format, half, "traceMain", "SsrPass Trace");
	build_pipeline(m_resolve_pipeline, k_reflection_format, half, "resolveMain", "SsrPass Resolve");
	build_pipeline(m_temporal_pipeline, k_reflection_format, half, "temporalMain", "SsrPass Temporal");
	build_pipeline(m_composite_pipeline, scene_format, extent, "compositeMain", "SsrPass Composite");

	createResources(core);
	createTargets(core, extent);
	TOAST_INFO("Render", "SsrPass ready ({}, GGX rays traced at half resolution)", vk::to_string(scene_format));
}

void SsrPass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "SsrPass shader layout has no descriptor sets");
		return;
	}

	const auto sampler_ci = linearClampSamplerInfo();
	m_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(core, *m_sampler, "SsrPass Sampler");

	// Nearest since interpolated depth or normals invent surfaces
	const auto point_ci = nearestClampSamplerInfo();
	m_point_sampler = vk::raii::Sampler(device, point_ci);
	setDebugName(core, *m_point_sampler, "SsrPass PointSampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_descriptor_sets.clear();
	m_bound_views.assign(VulkanRenderer::k_frames_in_flight * 2, {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight * 2; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("SsrPass DescriptorSet[{}]", i));
	}
}

void SsrPass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	const vk::Extent2D half = PostProcessTarget::halfExtent(extent);
	m_target.create(core, extent, m_scene_format, "SsrPass");
	m_hit_target.create(core, half, k_hit_format, "SsrPass Hits");
	m_resolve_target.create(core, half, k_reflection_format, "SsrPass Resolve");
	m_history[0].create(core, half, k_reflection_format, "SsrPass History0");
	m_history[1].create(core, half, k_reflection_format, "SsrPass History1");
	std::ranges::fill(m_bound_views, std::array<vk::ImageView, 2> {});
	m_history_valid = false;
}

void SsrPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr) {
		createTargets(*m_core, extent);
	}
}

auto SsrPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	const bool pipelines_ready = m_trace_pipeline.isReady() && m_resolve_pipeline.isReady() && m_temporal_pipeline.isReady() &&
	                             m_composite_pipeline.isReady();
	const bool targets_ready = m_target.isReady() && m_hit_target.isReady() && m_resolve_target.isReady() &&
	                           m_history[0].isReady() && m_history[1].isReady();
	if (!pipelines_ready || !targets_ready || (frame_index * 2) + 1 >= m_descriptor_sets.size() || !source_view) {
		return source_view;
	}

	const vk::ImageView depth_view = VulkanRenderer::instance->getDepthView();
	const vk::ImageView normal_view = VulkanRenderer::instance->getSceneNormalView();
	const vk::ImageView motion_view = VulkanRenderer::instance->getSceneMotionView();
	if (!depth_view || !normal_view || !motion_view) {
		return source_view;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const uint64_t sequence = frame != nullptr ? frame->sequence : 0;
	// A repeated snapshot carries no new sample so the last output stands
	if (m_history_valid && sequence == m_last_sequence) {
		return m_target.view();
	}

	const PostProcessTarget& history_write = m_history[m_parity];
	const PostProcessTarget& history_read = m_history[1 - m_parity];
	const uint32_t set_index = (frame_index * 2) + m_parity;
	const vk::DescriptorSet set = *m_descriptor_sets[set_index];

	const vk::ImageView previous_view = VulkanRenderer::instance->previousResolvedColour();
	const std::array<vk::ImageView, 2> wanted {source_view, previous_view ? previous_view : source_view};
	if (m_bound_views[set_index] != wanted) {
		// Point sampled except the colours since interpolated depth or normals invent surfaces
		const std::array<vk::DescriptorImageInfo, k_binding_count> image_infos {
		  vk::DescriptorImageInfo(*m_sampler, source_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  // Depth stays eDepthReadOnlyOptimal for the overlay stage
		  vk::DescriptorImageInfo(*m_point_sampler, depth_view, vk::ImageLayout::eDepthReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, normal_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, m_hit_target.view(), vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, m_resolve_target.view(), vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_sampler, history_read.view(), vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, motion_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  // Point sampled since the upsample picks its own texels
		  vk::DescriptorImageInfo(*m_point_sampler, history_write.view(), vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_sampler, wanted[1], vk::ImageLayout::eShaderReadOnlyOptimal),
		};

		std::array<vk::WriteDescriptorSet, k_binding_count> writes {};
		for (uint32_t binding = 0; binding < writes.size(); ++binding) {
			writes[binding] =
			    vk::WriteDescriptorSet(set, binding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_infos[binding]);
		}
		m_core->getDevice().updateDescriptorSets(writes, {});
		m_bound_views[set_index] = wanted;
	}

	const bool history_valid = m_history_valid && sequence == m_last_sequence + 1;
	if (!history_valid) {
		history_read.makeSampleable(cmd);
	}
	const auto settings = frame != nullptr ? frame->post_process.ssr : VulkanRenderer::PostProcessSettings::Ssr {};

	// Jittered since depth was rasterized with it
	const glm::mat4 view_projection = frame != nullptr ? frame->frame_data.jittered_view_projection : glm::mat4(1.0f);

	Params params {};
	params.view_projection = view_projection;
	params.inverse_view_projection = glm::inverse(view_projection);
	params.camera_position =
	    glm::vec4(frame != nullptr ? frame->frame_data.camera_position : glm::vec3(0.0f), static_cast<float>(sequence % 4096));
	params.tuning =
	    glm::vec4(settings.intensity, settings.max_roughness, settings.thickness, static_cast<float>(settings.max_steps));
	params.marching = glm::vec4(
	    settings.stride * static_cast<float>(settings.max_steps),
	    static_cast<float>(frame != nullptr ? frame->render_mode : 0u),
	    history_valid ? 1.0f : 0.0f,
	    previous_view ? 1.0f : 0.0f
	);

	const auto draw = [&](const PostProcessTarget& target, const VulkanPipeline& pipeline) {
		target.beginScope(cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eGraphics, *m_shader_layout.getPipelineLayout(), 0, std::array<vk::DescriptorSet, 1> {set}, {}
		);
		cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
		cmd.draw(3, 1, 0, 0);
		target.endScope(cmd);
	};

	draw(m_hit_target, m_trace_pipeline);
	draw(m_resolve_target, m_resolve_pipeline);
	draw(history_write, m_temporal_pipeline);
	draw(m_target, m_composite_pipeline);

	m_parity = 1 - m_parity;
	m_history_valid = true;
	m_last_sequence = sequence;
	return m_target.view();
}

}
