/**
 * @file taa_pass.cpp
 * @author dario
 * @date 25/09/2026
 */

#include "toast/renderer/passes/post/taa_pass.hpp"

#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

namespace renderer {

TaaPass::TaaPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format),
      m_dlss(core),
      m_motion_fill(core, VulkanRenderer::instance->renderExtent()) {
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
	m_bound_views.resize(VulkanRenderer::k_frames_in_flight);
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("TaaPass DescriptorSet[{}]", i));
	}
}

void TaaPass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	// DLSS writes the resolve as a storage image, R16G16B16A16Sfloat storage support is mandatory in Vulkan
	// and clears it on a reset so it needs the transfer destination too
	const vk::ImageUsageFlags usage =
	    m_dlss.available() ? vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst : vk::ImageUsageFlags {};
	m_targets[0].create(core, extent, m_scene_format, "TaaPass History A", usage);
	m_targets[1].create(core, extent, m_scene_format, "TaaPass History B", usage);
	m_bound_views.invalidate();
	m_history_valid = false;
}

void TaaPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr) {
		// The renderer waits for the device before resizing so the feature is idle
		m_dlss.releaseFeature();
		createTargets(*m_core, extent);
		// The renderer applies a new render scale before resizing the passes
		m_motion_fill.onResize(VulkanRenderer::instance->renderExtent());
	}
}

auto TaaPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const bool show_motion = frame != nullptr && frame->render_mode == k_render_mode_motion;
	const bool active = frame != nullptr && frame->taa_active;
	const glm::vec2 output_size(static_cast<float>(m_targets[0].extent().width), static_cast<float>(m_targets[0].extent().height));
	const bool upscale = frame != nullptr && frame->viewport_extent != output_size && frame->viewport_extent.x > 0.0f;
	if (!active && !show_motion && !upscale) {
		m_history_valid = false;
		VulkanRenderer::instance->setDlssActive(false);
		return source_view;
	}

	const vk::ImageView depth_view = VulkanRenderer::instance->getDepthView();
	const vk::ImageView motion_view = m_motion_fill.motionView();
	if (!m_pipeline.isReady() || frame_index >= m_descriptor_sets.size() || !source_view || !depth_view || !motion_view ||
	    !m_targets[0].isReady() || !m_targets[1].isReady()) {
		m_history_valid = false;
		VulkanRenderer::instance->setDlssActive(false);
		return source_view;
	}

	// A repeated snapshot carries no new sample and keeps the last resolve's DLSS state
	if (active && m_history_valid && frame->sequence == m_last_sequence) {
		return m_targets[m_write ^ 1u].view();
	}

	const PostProcessTarget& target = m_targets[m_write];
	const PostProcessTarget& history = m_targets[m_write ^ 1u];
	const bool reset = !m_history_valid || frame->taa_reset || !active;
	if (reset) {
		// Only for a legal layout since a reset frame never reads the history
		history.beginScope(cmd);
		history.endScope(cmd);
	}

	const std::array<vk::ImageView, 3> wanted {source_view, history.view(), motion_view};
	if (m_bound_views.update(frame_index, wanted)) {
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
	}

	const glm::mat4 current_view_projection = frame->frame_data.view_projection;
	const glm::mat4 previous_view_projection = m_history_valid ? m_last_view_projection : current_view_projection;

	// A skipped frame breaks written motion so the fill falls back to camera reprojection
	const bool frames_skipped = m_history_valid && frame->sequence != m_last_sequence + 1;

	const auto width = static_cast<float>(target.extent().width);
	const auto height = static_cast<float>(target.extent().height);

	const glm::mat4 reprojection = previous_view_projection * glm::inverse(current_view_projection);

	Params params {};
	params.texel = glm::vec4(1.0f / width, 1.0f / height, width, height);
	params.tuning = glm::vec4(VulkanRenderer::instance->taaHistoryWeight(), reset ? 1.0f : 0.0f, 0.0f, show_motion ? 1.0f : 0.0f);
	params.source = glm::vec4(frame->viewport_extent, frame->jitter_ndc * 0.5f);
	params.adapt = glm::vec4(16.0f, 0.0f, 0.0f, 0.0f);

	if (!m_motion_fill.record(cmd, frame_index, reprojection, frames_skipped)) {
		m_history_valid = false;
		VulkanRenderer::instance->setDlssActive(false);
		return source_view;
	}

	// DLSS keeps its own history, so it restarts after any frame it did not resolve
	const bool dlss = active && !show_motion && VulkanRenderer::instance->dlssRequested() && m_dlss.available() &&
	                  recordDlss(
	                      cmd,
	                      source_view,
	                      target,
	                      frame->jitter_ndc * 0.5f * frame->viewport_extent,
	                      frame->taa_reset || !VulkanRenderer::instance->dlssActive()
	                  );
	if (!dlss) {
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
	}
	VulkanRenderer::instance->setDlssActive(dlss);

	const vk::ImageView output = target.view();
	m_write ^= 1u;

	// The motion view is not a resolve and must never become history
	m_history_valid = !show_motion && active;
	m_last_sequence = frame->sequence;
	m_last_view_projection = current_view_projection;
	return output;
}

auto TaaPass::recordDlss(
    vk::CommandBuffer cmd, vk::ImageView source_view, const PostProcessTarget& target, glm::vec2 jitter_pixels, bool reset
) -> bool {
	ZoneScoped;
	auto* renderer = VulkanRenderer::instance;
	const vk::Image color_image =
	    source_view == renderer->getSceneColorView() ? renderer->getSceneColorImage() : PostProcessTarget::imageOf(source_view);
	const sync::ImageState& depth = renderer->getDepthState();
	if (!color_image || !depth || !m_motion_fill.motionImage() || !m_motion_fill.reactiveImage()) {
		/*const char* missing = !color_image ? "the TAA source view is neither the scene colour nor a post process target"
		                      : !depth     ? "the depth image is missing"
		                      : !m_motion_fill.motionImage() ? "the motion image is missing"
		                                                     : "the reactive image is missing";
		m_dlss.setRuntimeReason(missing);
		if (!m_dlss_source_logged) {
		  m_dlss_source_logged = true;
		  TOAST_WARN("Render", "DLSS skipped: {}; using TAA", missing);
		}*/
		return false;
	}

	const vk::Extent2D render_extent = renderer->renderExtent();
	m_dlss.ensureFeature(cmd, render_extent, target.extent());

	// NGX samples the inputs from compute in shader read layouts and writes the output as a storage image
	sync::BarrierBatch barriers;
	barriers.image(target.state(), sync::Usage::compute_storage_write);
	barriers.image(depth.image, depth.range, depth.usage, sync::Usage::shader_sampled);
	barriers.flush(cmd);
	// NGX clears the output on a reset before it writes it, and only a later dependency orders after the layout transition
	barriers.memory(sync::Usage::compute_storage_write, sync::Usage::transfer_dst);
	barriers.flush(cmd);

	DlssInputs inputs {};
	inputs.color = source_view;
	inputs.depth = renderer->getDepthView();
	inputs.motion = m_motion_fill.motionView();
	inputs.reactive = m_motion_fill.reactiveView();
	inputs.output = target.view();
	inputs.color_image = color_image;
	inputs.depth_image = depth.image;
	inputs.motion_image = m_motion_fill.motionImage();
	inputs.reactive_image = m_motion_fill.reactiveImage();
	inputs.output_image = target.state().image;
	inputs.color_format = m_scene_format;
	inputs.depth_format = renderer->getDepthFormat();
	inputs.render_extent = render_extent;
	inputs.output_extent = target.extent();
	inputs.jitter_pixels = jitter_pixels;
	inputs.reset = reset;
	const bool evaluated = m_dlss.evaluate(cmd, inputs);

	// The overlay and later passes expect the tracked depth layout back
	barriers.image(depth.image, depth.range, sync::Usage::shader_sampled, depth.usage);
	if (evaluated) {
		barriers.image(target.state(), sync::Usage::shader_sampled);
	}
	barriers.flush(cmd);
	return evaluated;
}

}
