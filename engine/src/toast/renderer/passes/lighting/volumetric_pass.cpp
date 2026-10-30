/**
 * @file volumetric_pass.cpp
 * @author dario
 * @date 26/10/2026
 */

#include "toast/renderer/passes/lighting/volumetric_pass.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/core/gpu_timer.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/shaders/shader_compiler.hpp"
#include "toast/renderer/voxel/voxel_blas_cache.hpp"
#include "toast/renderer/voxel/voxel_gpu_storage.hpp"
#include "toast/renderer/voxel/voxel_trace_scene.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_volumetric_scene_set = 0;
constexpr uint32_t k_volumetric_storage_set = 1;
constexpr uint32_t k_volumetric_image_set = 2;

/// voxel_dda.slang gVoxelInstances and scene_trace.slang bindings in the storage set
constexpr uint32_t k_volumetric_instances_binding = 6;
constexpr uint32_t k_volumetric_trace_volumes_binding = 10;
constexpr uint32_t k_volumetric_trace_bounds_binding = 11;
constexpr uint32_t k_volumetric_trace_grid_binding = 12;
constexpr uint32_t k_volumetric_trace_brick_bases_binding = 14;
constexpr uint32_t k_volumetric_trace_bricks_binding = 15;

/// volumetrics.slang gDepth gHistory and gFogAmbient
constexpr uint32_t k_volumetric_depth_binding = 1;
constexpr uint32_t k_volumetric_history_binding = 4;
constexpr uint32_t k_volumetric_ambient_binding = 6;
constexpr uint32_t k_volumetric_previous_ambient_binding = 7;

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
		TOAST_ERROR("Render", "VolumetricPass shader layout has {} descriptor sets instead of 3", layouts.size());
		setEnabled(false);
		return;
	}
	const VoxelTraceScene* trace = VulkanRenderer::instance->voxelTraceScene();
	if (trace == nullptr) {
		TOAST_ERROR("Render", "VolumetricPass needs the voxel trace scene; volumetrics are disabled");
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
	VulkanPipeline::Config ambient_config;
	ambient_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	ambient_config.debug_name = "Volumetrics Ambient";
	ambient_config.shader_spirv = shader->spirv;
	ambient_config.pipeline_layout = *m_layout.getPipelineLayout();
	ambient_config.compute_entry = "ambientMain";
	m_ambient_pipeline.rebuild(core, ambient_config);

	const vk::Extent2D half = PostProcessTarget::halfExtent(extent);
	build_pipeline(m_scatter_pipeline, k_scatter_format, half, "scatterMain", "Volumetrics Scatter");
	build_pipeline(m_temporal_pipeline, k_scatter_format, half, "temporalMain", "Volumetrics Temporal");
	build_pipeline(m_composite_pipeline, scene_format, extent, "compositeMain", "Volumetrics Composite");

	m_scene_sets.create(core, shader->reflection, *layouts[k_volumetric_scene_set], "Volumetrics");
	const bool instances_bound = std::ranges::any_of(shader->reflection.bindings, [](const ShaderBinding& binding) {
		return binding.set == k_volumetric_storage_set && binding.binding == k_volumetric_instances_binding;
	});

	const auto& device = core.getDevice();
	// Nearest since interpolated depth invents surfaces
	m_point_sampler = vk::raii::Sampler(device, nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "Volumetrics PointSampler");
	m_linear_sampler = vk::raii::Sampler(device, linearClampSamplerInfo());
	setDebugName(core, *m_linear_sampler, "Volumetrics LinearSampler");

	const vk::DescriptorSetLayout storage_layout = *layouts[k_volumetric_storage_set];
	const vk::DescriptorSetLayout image_layout = *layouts[k_volumetric_image_set];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	constexpr auto storage = vk::DescriptorType::eStorageBuffer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo ambient_ci {};
		ambient_ci.size = sizeof(glm::vec4) * (VulkanRenderer::k_max_fog_volumes + 1);
		ambient_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		vma::AllocationCreateInfo ambient_alloc {};
		ambient_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
		m_ambient.push_back(core.getAllocator().createBuffer(ambient_ci, ambient_alloc));
		setDebugName(core, *m_ambient.back(), std::format("Volumetrics Ambient[{}]", i));
	}

	DescriptorWriter writer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto storage_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &storage_layout));
		m_storage_sets.push_back(std::move(storage_set[0]));
		setDebugName(core, *m_storage_sets.back(), std::format("Volumetrics StorageSet[{}]", i));
		const vk::DescriptorSet set = *m_storage_sets.back();
		writer.buffer(set, k_volumetric_trace_volumes_binding, storage, trace->volumeBuffer(i));
		writer.buffer(set, k_volumetric_trace_bounds_binding, storage, trace->boundsBuffer(i));
		writer.buffer(set, k_volumetric_trace_grid_binding, storage, trace->gridBuffer(i));
		if (instances_bound) {
			writer.buffer(set, k_volumetric_instances_binding, storage, trace->volumeBuffer(i));
		}
		if (ShaderCompiler::isRayQueryAvailable()) {
			const VoxelBlasCache* bricks = VulkanRenderer::instance->voxelBlasCache();
			writer.buffer(set, k_volumetric_trace_brick_bases_binding, storage, trace->brickBaseBuffer(i));
			writer.buffer(
			    set, k_volumetric_trace_bricks_binding, storage, bricks != nullptr ? bricks->poolBuffer() : trace->brickBaseBuffer(i)
			);
		}

		const uint32_t previous = (i + VulkanRenderer::k_frames_in_flight - 1) % VulkanRenderer::k_frames_in_flight;
		for (uint32_t parity = 0; parity < 2; ++parity) {
			auto image_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &image_layout));
			m_image_sets.push_back(std::move(image_set[0]));
			setDebugName(core, *m_image_sets.back(), std::format("Volumetrics ImageSet[{}][{}]", i, parity));
			writer.buffer(*m_image_sets.back(), k_volumetric_ambient_binding, storage, *m_ambient[i]);
			writer.buffer(*m_image_sets.back(), k_volumetric_previous_ambient_binding, storage, *m_ambient[previous]);
		}
	}
	writer.flush(device);
	m_storage_bound.assign(m_storage_sets.size(), nullptr);
	m_image_bound.assign(m_image_sets.size(), {});

	createTargets(core, extent, resolveQuality().divisor);
	m_ready = m_ambient_pipeline.isReady() && m_scatter_pipeline.isReady() && m_temporal_pipeline.isReady() &&
	          m_composite_pipeline.isReady() && !m_scene_sets.empty();
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
	    (frame->post_process.fog.density <= 0.0f && frame->fog_volumes.empty()) || frame_index >= m_storage_sets.size() ||
	    !frame->voxel_storage || !frame->voxel_storage->isReady()) {
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

	// Safe since the fence of this slot was waited on
	if (m_storage_bound[frame_index] != frame->voxel_storage) {
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_storage_sets[frame_index], *frame->voxel_storage);
		m_storage_bound[frame_index] = frame->voxel_storage;
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
	};
	if (std::ranges::any_of(views, [](vk::ImageView view) { return !view; })) {
		return source_view;
	}
	if (m_image_bound[set_index] != views) {
		DescriptorWriter writer;
		for (uint32_t binding = 0; binding < k_image_bindings; ++binding) {
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

	const bool history_valid = m_history_valid && frame->sequence == m_last_sequence + 1 && !frame->taa_reset;
	if (!history_valid) {
		history_read.makeSampleable(cmd);
	}

	const auto volume_count = static_cast<uint32_t>(std::min<size_t>(frame->fog_volumes.size(), VulkanRenderer::k_max_fog_volumes));
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
	const std::array<vk::DescriptorSet, 3> sets {m_scene_sets.get(frame_index), *m_storage_sets[frame_index], image_set};
	const auto draw = [&](const PostProcessTarget& target, const VulkanPipeline& pipeline) {
		target.beginScope(cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, k_volumetric_scene_set, sets, {});
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
		cmd.draw(3, 1, 0, 0);
		target.endScope(cmd);
	};

	{
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "Fog ambient");
		// Last frame wrote the probes this one blends with
		sync::BarrierBatch history;
		history.memory(sync::Usage::compute_storage_write, sync::Usage::compute_storage_read);
		history.flush(cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_ambient_pipeline.getPipeline());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, k_volumetric_scene_set, sets, {});
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
		cmd.dispatch(volume_count + 1, 1, 1);
		sync::BarrierBatch barriers;
		barriers.memory(sync::Usage::compute_storage_write, sync::Usage::fragment_storage_read);
		barriers.flush(cmd);
	}
	draw(m_scatter, m_scatter_pipeline);
	draw(history_write, m_temporal_pipeline);
	draw(m_target, m_composite_pipeline);

	m_parity = 1 - m_parity;
	m_history_valid = true;
	m_last_sequence = frame->sequence;
	return m_target.view();
}

}
