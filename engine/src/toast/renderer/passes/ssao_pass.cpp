/**
 * @file ssao_pass.cpp
 * @author dario
 * @date 07/08/2026
 */

#include "ssao_pass.hpp"

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

SsaoPass::SsaoPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format) {
	ZoneScoped;
	const uint32_t push_constant_limit = core.getPhysicalDevice().getProperties().limits.maxPushConstantsSize;
	if (push_constant_limit < sizeof(Params)) {
		TOAST_ERROR(
		    "Render",
		    "SsaoPass needs {} bytes of push constants but this device allows {}; ambient occlusion is disabled",
		    sizeof(Params),
		    push_constant_limit
		);
		setEnabled(false);
		return;
	}

	const auto uid = assets::resolveURI("core://shaders/ssao.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "SsaoPass shader core://shaders/ssao.slang unavailable; ambient occlusion is disabled");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "SsaoPass");

	const auto build_pipeline =
	    [&](VulkanPipeline& pipeline, vk::Format format, vk::Extent2D target_extent, const char* entry, const char* debug_name) {
		    VulkanPipeline::Config config;
		    config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		    config.debug_name = debug_name;
		    config.color_format = format;
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
	build_pipeline(m_trace_pipeline, k_occlusion_format, PostProcessTarget::halfExtent(extent), "aoMain", "SsaoPass Trace");
	build_pipeline(m_composite_pipeline, scene_format, extent, "compositeMain", "SsaoPass Composite");

	createResources(core);
	createTargets(core, extent);
	TOAST_INFO("Render", "SsaoPass ready ({}, occlusion traced at half resolution)", vk::to_string(scene_format));
}

void SsaoPass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "SsaoPass shader layout has no descriptor sets");
		return;
	}

	const auto sampler_ci = linearClampSamplerInfo();
	m_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(core, *m_sampler, "SsaoPass Sampler");

	const auto point_ci = nearestClampSamplerInfo();
	m_point_sampler = vk::raii::Sampler(device, point_ci);
	setDebugName(core, *m_point_sampler, "SsaoPass PointSampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_descriptor_sets.clear();
	m_bound_views.assign(VulkanRenderer::k_frames_in_flight, vk::ImageView {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("SsaoPass DescriptorSet[{}]", i));
	}
}

void SsaoPass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	m_target.create(core, extent, m_scene_format, "SsaoPass");
	m_occlusion_target.create(core, PostProcessTarget::halfExtent(extent), k_occlusion_format, "SsaoPass Occlusion");
	std::ranges::fill(m_bound_views, vk::ImageView {});
}

void SsaoPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr) {
		createTargets(*m_core, extent);
	}
}

auto SsaoPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	if (!m_trace_pipeline.isReady() || !m_composite_pipeline.isReady() || frame_index >= m_descriptor_sets.size() || !source_view ||
	    !m_target.isReady() || !m_occlusion_target.isReady()) {
		return source_view;
	}

	const vk::ImageView depth_view = VulkanRenderer::instance->getDepthView();
	const vk::ImageView normal_view = VulkanRenderer::instance->getSceneNormalView();
	const vk::ImageView indirect_view = VulkanRenderer::instance->getSceneIndirectView();
	if (!depth_view || !normal_view || !indirect_view) {
		return source_view;
	}

	if (m_bound_views[frame_index] != source_view) {
		const std::array image_infos {
		  vk::DescriptorImageInfo(*m_sampler, source_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, depth_view, vk::ImageLayout::eDepthReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, normal_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, indirect_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  // Point sampled since the upsample picks its own texels
		  vk::DescriptorImageInfo(*m_point_sampler, m_occlusion_target.view(), vk::ImageLayout::eShaderReadOnlyOptimal)
		};

		std::array<vk::WriteDescriptorSet, 5> writes {};
		for (uint32_t binding = 0; binding < writes.size(); ++binding) {
			writes[binding] = vk::WriteDescriptorSet(
			    *m_descriptor_sets[frame_index], binding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_infos[binding]
			);
		}
		m_core->getDevice().updateDescriptorSets(writes, {});
		m_bound_views[frame_index] = source_view;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const auto settings = frame != nullptr ? frame->post_process.ssao : VulkanRenderer::PostProcessSettings::Ssao {};

	const glm::mat4 view_projection = frame != nullptr ? frame->frame_data.view_projection : glm::mat4(1.0f);
	const glm::vec3 camera_position = frame != nullptr ? frame->frame_data.camera_position : glm::vec3(0.0f);

	Params params {};
	params.view_projection = view_projection;
	params.inverse_view_projection = glm::inverse(view_projection);
	params.tuning = glm::vec4(settings.radius, settings.strength, settings.range_cutoff, static_cast<float>(settings.sample_count));
	params.misc = glm::vec4(static_cast<float>(frame != nullptr ? frame->render_mode : 0u), camera_position);

	const auto draw = [&](const PostProcessTarget& target, const VulkanPipeline& pipeline) {
		const vk::Extent2D size = target.extent();
		params.screen_size = glm::vec4(
		    static_cast<float>(size.width),
		    static_cast<float>(size.height),
		    1.0f / static_cast<float>(size.width),
		    1.0f / static_cast<float>(size.height)
		);

		target.beginScope(cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
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
	};

	draw(m_occlusion_target, m_trace_pipeline);
	draw(m_target, m_composite_pipeline);

	return m_target.view();
}

}
