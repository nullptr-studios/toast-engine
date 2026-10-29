/**
 * @file motion_blur_pass.cpp
 * @author dario
 * @date 29/10/2026
 */

#include "motion_blur_pass.hpp"

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

MotionBlurPass::MotionBlurPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format) {
	ZoneScoped;
	static_assert(sizeof(Params) <= 128, "Vulkan guarantees 128 bytes of push constants");

	const auto uid = assets::resolveURI("core://shaders/motion_blur.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "MotionBlurPass shader core://shaders/motion_blur.slang unavailable; motion blur is disabled");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "MotionBlurPass");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "MotionBlurPass";
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
	m_target.create(core, extent, scene_format, "MotionBlurPass");
	TOAST_INFO("Render", "MotionBlurPass ready ({})", vk::to_string(scene_format));
}

void MotionBlurPass::createResources(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "MotionBlurPass shader layout has no descriptor sets");
		return;
	}

	m_linear_sampler = vk::raii::Sampler(device, linearClampSamplerInfo());
	setDebugName(core, *m_linear_sampler, "MotionBlurPass Sampler");

	m_point_sampler = vk::raii::Sampler(device, nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "MotionBlurPass PointSampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_descriptor_sets.clear();
	m_bound_views.assign(VulkanRenderer::k_frames_in_flight, {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("MotionBlurPass DescriptorSet[{}]", i));
	}
}

void MotionBlurPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr) {
		m_target.create(*m_core, extent, m_scene_format, "MotionBlurPass");
		std::ranges::fill(m_bound_views, vk::ImageView {});
	}
}

auto MotionBlurPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->render_mode != 0 || frame->taa_reset) {
		m_have_sequence = false;
		return source_view;
	}

	const vk::ImageView depth_view = VulkanRenderer::instance->getDepthView();
	const vk::ImageView motion_view = VulkanRenderer::instance->getSceneMotionView();
	if (!m_pipeline.isReady() || frame_index >= m_descriptor_sets.size() || !source_view || !depth_view || !motion_view ||
	    !m_target.isReady()) {
		return source_view;
	}

	// A repeated snapshot keeps the pose it was first drawn with
	bool frames_skipped = false;
	if (!m_have_sequence) {
		m_previous_view_projection = frame->frame_data.view_projection;
		m_view_projection = frame->frame_data.view_projection;
		m_sequence = frame->sequence;
		m_have_sequence = true;
	} else if (frame->sequence != m_sequence) {
		frames_skipped = frame->sequence != m_sequence + 1;
		m_previous_view_projection = m_view_projection;
		m_view_projection = frame->frame_data.view_projection;
		m_sequence = frame->sequence;
	}

	if (m_bound_views[frame_index] != source_view) {
		const std::array image_infos {
		  vk::DescriptorImageInfo(*m_linear_sampler, source_view, vk::ImageLayout::eShaderReadOnlyOptimal),
		  // Depth stays eDepthReadOnlyOptimal for the overlay stage
		  vk::DescriptorImageInfo(*m_point_sampler, depth_view, vk::ImageLayout::eDepthReadOnlyOptimal),
		  vk::DescriptorImageInfo(*m_point_sampler, motion_view, vk::ImageLayout::eShaderReadOnlyOptimal)
		};

		std::array<vk::WriteDescriptorSet, 3> writes {};
		for (uint32_t binding = 0; binding < writes.size(); ++binding) {
			writes[binding] = vk::WriteDescriptorSet(
			    *m_descriptor_sets[frame_index], binding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_infos[binding]
			);
		}
		m_core->getDevice().updateDescriptorSets(writes, {});
		m_bound_views[frame_index] = source_view;
	}

	const auto width = static_cast<float>(m_target.extent().width);
	const auto height = static_cast<float>(m_target.extent().height);
	const auto* vulkan = VulkanRenderer::instance;

	Params params {};
	params.reprojection = m_previous_view_projection * glm::inverse(m_view_projection);
	params.texel = glm::vec4(1.0f / width, 1.0f / height, width, height);
	params.tuning = glm::vec4(
	    vulkan->motionBlurStrength(),
	    vulkan->motionBlurMaxLength(),
	    static_cast<float>(vulkan->motionBlurSamples()),
	    frames_skipped ? 1.0f : 0.0f
	);
	params.camera = glm::vec4(frame->camera_near, frame->camera_far, 0.0f, 0.0f);

	m_target.beginScope(cmd);
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
	m_target.endScope(cmd);

	return m_target.view();
}

}
