/**
 * @file volumetric_pass.cpp
 * @author dario
 * @date 26/10/2026
 */

#include "volumetric_pass.hpp"

#include "../descriptor_writer.hpp"
#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_volumetric_scene_set = 0;
constexpr uint32_t k_volumetric_image_set = 1;

/// volumetrics.slang gDepth and gHistory
constexpr uint32_t k_volumetric_depth_binding = 1;
constexpr uint32_t k_volumetric_history_binding = 4;
constexpr uint32_t k_volumetric_fog_volumes_binding = 6;

/// Low Medium High Epic
constexpr std::array<VolumetricPass::Quality, VulkanRenderer::k_volumetric_quality_custom> k_volumetric_levels {
  {
   {.steps = 12, .lights = 2, .light_shadows = false, .divisor = 4},
   {.steps = 16, .lights = 4, .light_shadows = true, .divisor = 2},
   {.steps = 24, .lights = 8, .light_shadows = true, .divisor = 2},
   {.steps = 40, .lights = 16, .light_shadows = true, .divisor = 2},
   }
};

auto divided(vk::Extent2D extent, uint32_t divisor) -> vk::Extent2D {
	return {std::max((extent.width + divisor - 1) / divisor, 1u), std::max((extent.height + divisor - 1) / divisor, 1u)};
}

}

VolumetricPass::VolumetricPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format) {
	ZoneScoped;
	if (core.getPhysicalDevice().getProperties().limits.maxPushConstantsSize < sizeof(Params)) {
		TOAST_ERROR("Render", "VolumetricPass needs {} bytes of push constants; volumetrics are disabled", sizeof(Params));
		setEnabled(false);
		return;
	}
	const auto uid = assets::resolveURI("core://shaders/volumetrics.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "VolumetricPass shader core://shaders/volumetrics.slang unavailable; volumetrics are disabled");
		setEnabled(false);
		return;
	}

	m_layout.rebuild(core, shader->reflection, "VolumetricPass");
	const auto& layouts = m_layout.getDescriptorSetLayouts();
	if (layouts.size() <= k_volumetric_image_set) {
		TOAST_ERROR("Render", "VolumetricPass shader layout has {} descriptor sets instead of 2", layouts.size());
		setEnabled(false);
		return;
	}

	const auto build_pipeline =
	    [&](VulkanPipeline& pipeline, vk::Format format, vk::Extent2D target_extent, const char* entry, const char* debug_name) {
		    VulkanPipeline::Config config;
		    config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		    config.debug_name = debug_name;
		    config.color_format = format;
		    // Transmittance lives in alpha
		    config.write_color_alpha = true;
		    config.extent = target_extent;
		    config.shader_spirv = shader->spirv;
		    config.fragment_entry = entry;
		    config.pipeline_layout = *m_layout.getPipelineLayout();
		    config.vertex_bindings = {};
		    config.vertex_attributes = {};
		    config.cull_mode = vk::CullModeFlagBits::eNone;
		    config.depth_test = false;
		    config.depth_write = false;
		    pipeline.rebuild(core, config);
	    };
	const vk::Extent2D half = PostProcessTarget::halfExtent(extent);
	build_pipeline(m_scatter_pipeline, k_scatter_format, half, "scatterMain", "Volumetrics Scatter");
	build_pipeline(m_temporal_pipeline, k_scatter_format, half, "temporalMain", "Volumetrics Temporal");
	build_pipeline(m_composite_pipeline, scene_format, extent, "compositeMain", "Volumetrics Composite");

	m_scene_sets.create(core, shader->reflection, *layouts[k_volumetric_scene_set], "Volumetrics");

	const auto& device = core.getDevice();
	// Nearest since interpolated depth invents surfaces
	m_point_sampler = vk::raii::Sampler(device, nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "Volumetrics PointSampler");
	m_linear_sampler = vk::raii::Sampler(device, linearClampSamplerInfo());
	setDebugName(core, *m_linear_sampler, "Volumetrics LinearSampler");

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(VulkanRenderer::FogVolumeGpu) * VulkanRenderer::k_max_fog_volumes;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAuto;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
		m_fog_volumes.push_back(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(core, *m_fog_volumes.back(), std::format("Volumetrics FogVolumes[{}]", i));
	}

	const vk::DescriptorSetLayout image_layout = *layouts[k_volumetric_image_set];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	DescriptorWriter writer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight * 2; ++i) {
		auto allocated = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &image_layout));
		m_image_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_image_sets.back(), std::format("Volumetrics ImageSet[{}]", i));
		writer.buffer(
		    *m_image_sets.back(), k_volumetric_fog_volumes_binding, vk::DescriptorType::eStorageBuffer, *m_fog_volumes[i / 2]
		);
	}
	writer.flush(device);
	m_image_bound.assign(m_image_sets.size(), {});

	createTargets(core, extent, resolveQuality().divisor);
	m_ready =
	    m_scatter_pipeline.isReady() && m_temporal_pipeline.isReady() && m_composite_pipeline.isReady() && !m_scene_sets.empty();
	if (m_ready) {
		TOAST_INFO(
		    "Render",
		    "VolumetricPass ready (height fog and up to {} fog volumes marched at half resolution)",
		    VulkanRenderer::k_max_fog_volumes
		);
	}
}

auto VolumetricPass::resolveQuality() -> Quality {
	const auto* renderer = VulkanRenderer::instance;
	const uint32_t level = renderer->volumetricQuality();
	if (level < k_volumetric_levels.size()) {
		return k_volumetric_levels[level];
	}
	return Quality {
	  .steps = renderer->volumetricSteps(),
	  .lights = renderer->volumetricLights(),
	  .light_shadows = renderer->volumetricLightShadows(),
	  .divisor = renderer->volumetricQuarterResolution() ? 4u : 2u,
	};
}

void VolumetricPass::createTargets(const VulkanCore& core, vk::Extent2D extent, uint32_t divisor) {
	m_extent = extent;
	m_divisor = divisor;
	const vk::Extent2D reduced = divided(extent, divisor);
	m_target.create(core, extent, m_scene_format, "Volumetrics");
	m_scatter.create(core, reduced, k_scatter_format, "Volumetrics Scatter");
	m_history[0].create(core, reduced, k_scatter_format, "Volumetrics History0");
	m_history[1].create(core, reduced, k_scatter_format, "Volumetrics History1");
	std::ranges::fill(m_image_bound, std::array<vk::ImageView, k_image_bindings> {});
	m_history_valid = false;
}

void VolumetricPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr && m_ready) {
		createTargets(*m_core, extent, m_divisor);
	}
}

auto VolumetricPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	auto* renderer = VulkanRenderer::instance;
	const auto* frame = renderer->renderingFrame();
	if (!m_ready || !source_view || frame == nullptr || !renderer->volumetrics() || frame->render_mode != 0 ||
	    (frame->post_process.fog.density <= 0.0f && frame->fog_volumes.empty()) || (frame_index * 2) + 1 >= m_image_sets.size()) {
		m_history_valid = false;
		return source_view;
	}
	// A repeated snapshot carries no new sample so the last output stands
	if (m_history_valid && frame->sequence == m_last_sequence) {
		return m_target.view();
	}

	// Frames in flight still sample the old targets
	const Quality quality = resolveQuality();
	if (quality.divisor != m_divisor) {
		m_core->getDevice().waitIdle();
		createTargets(*m_core, m_extent, quality.divisor);
	}

	const PostProcessTarget& history_write = m_history[m_parity];
	const PostProcessTarget& history_read = m_history[1 - m_parity];
	const uint32_t set_index = (frame_index * 2) + m_parity;
	const vk::DescriptorSet image_set = *m_image_sets[set_index];
	const std::array<vk::ImageView, k_image_bindings> views {
	  source_view,
	  renderer->getDepthView(),
	  renderer->getSceneMotionView(),
	  m_scatter.view(),
	  history_read.view(),
	  history_write.view(),
	  vk::ImageView {},
	  renderer->getSceneNormalView(),
	};
	for (uint32_t binding = 0; binding < k_image_bindings; ++binding) {
		if (binding != k_volumetric_fog_volumes_binding && !views[binding]) {
			return source_view;
		}
	}
	if (m_image_bound[set_index] != views) {
		DescriptorWriter writer;
		for (uint32_t binding = 0; binding < k_image_bindings; ++binding) {
			// The fog volume buffer was written once at creation
			if (binding == k_volumetric_fog_volumes_binding) {
				continue;
			}
			writer.image(
			    image_set,
			    binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    binding == k_volumetric_history_binding ? *m_linear_sampler : *m_point_sampler,
			    views[binding],
			    binding == k_volumetric_depth_binding ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal
			);
		}
		writer.flush(m_core->getDevice());
		m_image_bound[set_index] = views;
	}
	m_scene_sets.updateTlas(frame_index);

	// Safe since the fence of this slot was waited on
	const auto volume_count = static_cast<uint32_t>(std::min<size_t>(frame->fog_volumes.size(), VulkanRenderer::k_max_fog_volumes));
	if (volume_count > 0) {
		const auto& allocation = m_fog_volumes[frame_index].getAllocation();
		std::memcpy(allocation.getInfo().pMappedData, frame->fog_volumes.data(), sizeof(VulkanRenderer::FogVolumeGpu) * volume_count);
		allocation.flush(0, sizeof(VulkanRenderer::FogVolumeGpu) * volume_count);
	}

	const bool history_valid = m_history_valid && frame->sequence == m_last_sequence + 1 && !frame->taa_reset;
	if (!history_valid) {
		history_read.makeSampleable(cmd);
	}

	const auto& fog = frame->post_process.fog;
	Params params {};
	params.inverse_view_projection = glm::inverse(frame->frame_data.jittered_view_projection);
	params.medium = glm::vec4(fog.density, fog.height, fog.height_falloff, fog.anisotropy);
	params.albedo = glm::vec4(fog.albedo_r, fog.albedo_g, fog.albedo_b, fog.ambient);
	params.marching =
	    glm::vec4(fog.distance, static_cast<float>(quality.steps), history_valid ? 1.0f : 0.0f, static_cast<float>(volume_count));
	params.quality = glm::vec4(
	    static_cast<float>(m_divisor),
	    static_cast<float>(quality.lights),
	    quality.light_shadows ? 1.0f : 0.0f,
	    renderer->volumetricDistance()
	);

	const vk::PipelineLayout layout = *m_layout.getPipelineLayout();
	const std::array<vk::DescriptorSet, 2> sets {m_scene_sets.get(frame_index), image_set};
	const auto draw = [&](const PostProcessTarget& target, const VulkanPipeline& pipeline) {
		target.beginScope(cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, k_volumetric_scene_set, sets, {});
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
		cmd.draw(3, 1, 0, 0);
		target.endScope(cmd);
	};

	draw(m_scatter, m_scatter_pipeline);
	draw(history_write, m_temporal_pipeline);
	draw(m_target, m_composite_pipeline);

	m_parity = 1 - m_parity;
	m_history_valid = true;
	m_last_sequence = frame->sequence;
	return m_target.view();
}

}
