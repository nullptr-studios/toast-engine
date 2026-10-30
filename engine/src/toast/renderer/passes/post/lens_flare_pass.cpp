/**
 * @file lens_flare_pass.cpp
 * @author dario
 * @date 29/10/2026
 */

#include "toast/renderer/passes/post/lens_flare_pass.hpp"

#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_min_size = 8;
constexpr uint32_t k_glare_vertices = 18;

auto scaledExtent(vk::Extent2D extent, uint32_t divisor) -> vk::Extent2D {
	return {std::max(extent.width / divisor, k_min_size), std::max(extent.height / divisor, k_min_size)};
}

}

LensFlarePass::LensFlarePass(const VulkanCore& core, vk::Format hdr_format, vk::Extent2D extent)
    : m_core(&core),
      m_format(hdr_format) {
	ZoneScoped;
	static_assert(sizeof(Params) <= 128, "Vulkan guarantees 128 bytes of push constants");

	const auto uid = assets::resolveURI("core://shaders/lens_flare.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "LensFlarePass shader core://shaders/lens_flare.slang unavailable; lens flares are disabled");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "LensFlarePass");
	m_sampler = vk::raii::Sampler(core.getDevice(), linearClampSamplerInfo());
	setDebugName(core, *m_sampler, "LensFlarePass Sampler");

	createPipelines(core);
	createTargets(core, extent);
}

void LensFlarePass::createPipelines(const VulkanCore& core) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/lens_flare.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.color_format = m_format;
	config.extent = vk::Extent2D {1, 1};
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;

	const auto build = [&](VulkanPipeline& pipeline, const char* label, const char* fragment, VulkanPipeline::BlendPreset blend) {
		config.debug_name = std::format("LensFlarePass {}", label);
		config.fragment_entry = fragment;
		config.blend_preset = blend;
		pipeline.rebuild(core, config);
	};

	using Blend = VulkanPipeline::BlendPreset;
	build(m_threshold_pipeline, "Threshold", "fragmentThreshold", Blend::none);
	build(m_blur_down_pipeline, "BlurDown", "fragmentBlurDown", Blend::none);
	build(m_blur_up_pipeline, "BlurUp", "fragmentBlurUp", Blend::none);
	build(m_chroma_pipeline, "Chroma", "fragmentChroma", Blend::none);
	build(m_ghosts_pipeline, "Ghosts", "fragmentGhosts", Blend::none);
	build(m_halo_pipeline, "Halo", "fragmentHalo", Blend::additive);
	build(m_mix_pipeline, "Mix", "fragmentMix", Blend::none);

	config.vertex_entry = "glareVertex";
	build(m_glare_pipeline, "Glare", "fragmentGlare", Blend::additive);
}

void LensFlarePass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	ZoneScoped;
	const auto& device = core.getDevice();

	const auto make_target = [&](Target& target, vk::Extent2D size, std::string_view debug_name) {
		target = {};
		target.extent = size;

		const auto image_ci = colorTargetImageInfo(size, m_format);
		vma::AllocationCreateInfo allocation_ci {};
		allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

		target.image.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
		setDebugName(core, **target.image, std::string(debug_name));
		target.state.reset(**target.image, colorSubresourceRange());

		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **target.image;
		view_ci.viewType = vk::ImageViewType::e2D;
		view_ci.format = m_format;
		view_ci.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		target.view.emplace(device, view_ci);
		setDebugName(core, **target.view, std::format("{} View", debug_name));
	};

	const vk::Extent2D quarter = scaledExtent(extent, 4);
	make_target(m_quarter_a, quarter, "LensFlarePass QuarterA");
	make_target(m_quarter_b, quarter, "LensFlarePass QuarterB");
	make_target(m_flares, quarter, "LensFlarePass Flares");
	make_target(m_glare_target, quarter, "LensFlarePass Glare");
	make_target(m_eighth, scaledExtent(extent, 8), "LensFlarePass Eighth");
	make_target(m_composite, extent, "LensFlarePass Composite");

	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_frame_sets.clear();
	m_frame_sets.resize(VulkanRenderer::k_frames_in_flight);
	for (size_t frame = 0; frame < m_frame_sets.size(); ++frame) {
		for (uint32_t step = 0; step < step_count; ++step) {
			const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &set_layout);
			auto allocated = device.allocateDescriptorSets(alloc_info);
			m_frame_sets[frame].sets.push_back(std::move(allocated[0]));
			setDebugName(core, *m_frame_sets[frame].sets.back(), std::format("LensFlarePass Set[{}][{}]", frame, step));
		}
	}

	TOAST_INFO("Render", "LensFlarePass ready: {}x{} flare targets", quarter.width, quarter.height);
}

void LensFlarePass::writeDescriptor(vk::DescriptorSet set, vk::ImageView source, vk::ImageView scene, vk::ImageView glare) {
	const std::array<vk::DescriptorImageInfo, 3> image_infos {
	  vk::DescriptorImageInfo(*m_sampler, source, vk::ImageLayout::eShaderReadOnlyOptimal),
	  vk::DescriptorImageInfo(*m_sampler, scene ? scene : source, vk::ImageLayout::eShaderReadOnlyOptimal),
	  vk::DescriptorImageInfo(*m_sampler, glare ? glare : source, vk::ImageLayout::eShaderReadOnlyOptimal),
	};
	std::array<vk::WriteDescriptorSet, 3> writes {};
	for (uint32_t binding = 0; binding < writes.size(); ++binding) {
		writes[binding] =
		    vk::WriteDescriptorSet(set, binding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_infos[binding]);
	}
	m_core->getDevice().updateDescriptorSets(writes, {});
}

void LensFlarePass::draw(
    vk::CommandBuffer cmd, const VulkanPipeline& pipeline, vk::DescriptorSet set, Target& target, const Params& params,
    vk::AttachmentLoadOp load, uint32_t vertex_count, uint32_t instance_count
) {
	sync::transition(cmd, target.state, sync::Usage::color_attachment);

	vk::RenderingAttachmentInfo attachment {};
	attachment.imageView = **target.view;
	attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	attachment.loadOp = load;
	attachment.storeOp = vk::AttachmentStoreOp::eStore;
	attachment.clearValue = vk::ClearValue(vk::ClearColorValue(std::array<float, 4> {0.0f, 0.0f, 0.0f, 1.0f}));

	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, target.extent);
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &attachment;

	cmd.beginRendering(rendering_info);
	cmd.setViewport(
	    0,
	    std::array {
	      vk::Viewport(0.0f, 0.0f, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0.0f, 1.0f)
	    }
	);
	cmd.setScissor(0, std::array {vk::Rect2D({0, 0}, target.extent)});
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics, *m_shader_layout.getPipelineLayout(), 0, std::array<vk::DescriptorSet, 1> {set}, {}
	);
	cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
	cmd.draw(vertex_count, instance_count, 0, 0);
	cmd.endRendering();

	sync::transition(cmd, target.state, sync::Usage::shader_sampled);
}

auto LensFlarePass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	if (!m_composite.view.has_value() || !m_mix_pipeline.isReady() || !m_glare_pipeline.isReady() || !source_view ||
	    frame_index >= m_frame_sets.size() || m_frame_sets[frame_index].sets.size() != step_count) {
		return source_view;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->render_mode != 0) {
		return source_view;
	}
	const auto settings = frame->post_process.lens_flare;
	if (settings.intensity <= 0.0f) {
		return source_view;
	}

	// Safe since the fence of this slot was waited on
	FrameSets& sets = m_frame_sets[frame_index];
	const vk::ImageView quarter_a = **m_quarter_a.view;
	const vk::ImageView quarter_b = **m_quarter_b.view;
	if (sets.bound_scene != source_view) {
		const auto set = [&sets](Step step) { return *sets.sets[step]; };
		writeDescriptor(set(step_threshold), source_view, source_view, source_view);
		writeDescriptor(set(step_glare), quarter_a, quarter_a, quarter_a);
		writeDescriptor(set(step_blur_down), quarter_a, quarter_a, quarter_a);
		writeDescriptor(set(step_blur_up), **m_eighth.view, **m_eighth.view, **m_eighth.view);
		writeDescriptor(set(step_chroma), quarter_b, quarter_b, quarter_b);
		writeDescriptor(set(step_ghosts), quarter_a, quarter_a, quarter_a);
		writeDescriptor(set(step_halo), quarter_b, quarter_b, quarter_b);
		writeDescriptor(set(step_mix), **m_flares.view, source_view, **m_glare_target.view);
		sets.bound_scene = source_view;
	}

	const auto inverse = [](vk::Extent2D size) {
		return glm::vec2(1.0f / static_cast<float>(size.width), 1.0f / static_cast<float>(size.height));
	};
	const vk::Extent2D output = m_composite.extent;

	Params params {};
	params.flare = glm::vec4(settings.threshold, settings.threshold_range, settings.ghost_intensity, settings.chroma_shift);
	params.halo = glm::vec4(settings.halo_intensity, settings.halo_width, settings.halo_mask, settings.halo_compression);
	params.glare = glm::vec4(
	    settings.glare_intensity, settings.glare_divider, static_cast<float>(output.width) / static_cast<float>(output.height), 0.0f
	);
	params.tint = glm::vec4(1.0f, 1.0f, 1.0f, settings.intensity);

	const auto texel = [&](vk::Extent2D source, vk::Extent2D target, float source_scale) {
		return glm::vec4(inverse(source) * source_scale, inverse(target));
	};
	const uint32_t glare_instances = ((m_quarter_a.extent.width + 1) / 2) * ((m_quarter_a.extent.height + 1) / 2);

	using Load = vk::AttachmentLoadOp;
	params.texel = texel(output, m_quarter_a.extent, 2.0f);
	draw(cmd, m_threshold_pipeline, *sets.sets[step_threshold], m_quarter_a, params, Load::eDontCare, 3, 1);

	params.texel = texel(m_quarter_a.extent, m_glare_target.extent, 1.0f);
	draw(cmd, m_glare_pipeline, *sets.sets[step_glare], m_glare_target, params, Load::eClear, k_glare_vertices, glare_instances);

	params.texel = texel(m_quarter_a.extent, m_eighth.extent, 1.0f);
	draw(cmd, m_blur_down_pipeline, *sets.sets[step_blur_down], m_eighth, params, Load::eDontCare, 3, 1);

	params.texel = texel(m_eighth.extent, m_quarter_b.extent, 1.0f);
	draw(cmd, m_blur_up_pipeline, *sets.sets[step_blur_up], m_quarter_b, params, Load::eDontCare, 3, 1);

	draw(cmd, m_chroma_pipeline, *sets.sets[step_chroma], m_quarter_a, params, Load::eDontCare, 3, 1);
	draw(cmd, m_ghosts_pipeline, *sets.sets[step_ghosts], m_flares, params, Load::eDontCare, 3, 1);
	draw(cmd, m_halo_pipeline, *sets.sets[step_halo], m_flares, params, Load::eLoad, 3, 1);

	params.texel = texel(m_flares.extent, output, 1.0f);
	params.texel.z = 1.0f / static_cast<float>(m_glare_target.extent.width);
	params.texel.w = 1.0f / static_cast<float>(m_glare_target.extent.height);
	draw(cmd, m_mix_pipeline, *sets.sets[step_mix], m_composite, params, Load::eDontCare, 3, 1);

	return **m_composite.view;
}

void LensFlarePass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr && m_mix_pipeline.isReady()) {
		createTargets(*m_core, extent);
		for (FrameSets& sets : m_frame_sets) {
			sets.bound_scene = nullptr;
		}
	}
}

}
