/**
 * @file traced_specular_pass.cpp
 * @author dario
 * @date 25/10/2026
 */

#include "traced_specular_pass.hpp"

#include "../descriptor_writer.hpp"
#include "../shader_cache.hpp"
#include "../shader_compiler.hpp"
#include "../voxel_blas_cache.hpp"
#include "../voxel_gpu_storage.hpp"
#include "../voxel_trace_scene.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_specular_scene_set = 0;
constexpr uint32_t k_specular_storage_set = 1;
constexpr uint32_t k_specular_image_set = 2;

/// voxel_dda.slang gVoxelInstances and scene_trace.slang bindings in the storage set
constexpr uint32_t k_specular_instances_binding = 6;
constexpr uint32_t k_specular_trace_volumes_binding = 10;
constexpr uint32_t k_specular_trace_bounds_binding = 11;
constexpr uint32_t k_specular_trace_grid_binding = 12;
constexpr uint32_t k_specular_trace_brick_bases_binding = 14;
constexpr uint32_t k_specular_trace_bricks_binding = 15;

/// voxel_specular.slang gDepth
constexpr uint32_t k_specular_depth_binding = 1;

}

TracedSpecularPass::TracedSpecularPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/voxel_specular.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR(
		    "Render",
		    "TracedSpecularPass shader core://shaders/voxel_specular.slang unavailable; voxels keep the environment specular"
		);
		setEnabled(false);
		return;
	}

	m_layout.rebuild(core, shader->reflection, "TracedSpecularPass");
	const auto& layouts = m_layout.getDescriptorSetLayouts();
	if (layouts.size() <= k_specular_image_set) {
		TOAST_ERROR("Render", "TracedSpecularPass shader layout has {} descriptor sets instead of 3", layouts.size());
		setEnabled(false);
		return;
	}

	const auto build_pipeline =
	    [&](VulkanPipeline& pipeline, vk::Format format, vk::Extent2D target_extent, const char* entry, const char* debug_name) {
		    VulkanPipeline::Config config;
		    config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		    config.debug_name = debug_name;
		    config.color_format = format;
		    // Roughness and the no trace mark live in alpha
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
	build_pipeline(m_trace_pipeline, k_trace_format, half, "traceMain", "TracedSpecular Trace");
	build_pipeline(m_resolve_pipeline, k_trace_format, half, "resolveMain", "TracedSpecular Resolve");
	build_pipeline(m_temporal_pipeline, k_trace_format, half, "temporalMain", "TracedSpecular Temporal");
	build_pipeline(m_composite_pipeline, scene_format, extent, "compositeMain", "TracedSpecular Composite");

	m_scene_sets.create(core, shader->reflection, *layouts[k_specular_scene_set], "TracedSpecular");
	m_instances_bound = std::ranges::any_of(shader->reflection.bindings, [](const ShaderBinding& binding) {
		return binding.set == k_specular_storage_set && binding.binding == k_specular_instances_binding;
	});
	createSets(core);
	createTargets(core, extent);

	m_ready = m_trace_pipeline.isReady() && m_resolve_pipeline.isReady() && m_temporal_pipeline.isReady() &&
	          m_composite_pipeline.isReady() && !m_scene_sets.empty() && !m_storage_sets.empty();
	if (m_ready) {
		TOAST_INFO("Render", "TracedSpecularPass ready (GGX rays traced at half resolution)");
	}
}

void TracedSpecularPass::createSets(const VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_layout.getDescriptorSetLayouts();
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const VoxelTraceScene* trace = VulkanRenderer::instance->voxelTraceScene();
	if (trace == nullptr) {
		TOAST_ERROR("Render", "TracedSpecularPass needs the voxel trace scene; voxels keep the environment specular");
		return;
	}

	// Nearest since interpolated depth or normals invent surfaces
	m_point_sampler = vk::raii::Sampler(device, nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "TracedSpecular PointSampler");

	const vk::DescriptorSetLayout storage_layout = *layouts[k_specular_storage_set];
	const vk::DescriptorSetLayout image_layout = *layouts[k_specular_image_set];
	constexpr auto storage = vk::DescriptorType::eStorageBuffer;
	m_storage_sets.clear();
	m_image_sets.clear();
	DescriptorWriter writer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto storage_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &storage_layout));
		m_storage_sets.push_back(std::move(storage_set[0]));
		setDebugName(core, *m_storage_sets.back(), std::format("TracedSpecular StorageSet[{}]", i));
		const vk::DescriptorSet set = *m_storage_sets.back();
		writer.buffer(set, k_specular_trace_volumes_binding, storage, trace->volumeBuffer(i));
		writer.buffer(set, k_specular_trace_bounds_binding, storage, trace->boundsBuffer(i));
		writer.buffer(set, k_specular_trace_grid_binding, storage, trace->gridBuffer(i));
		if (m_instances_bound) {
			writer.buffer(set, k_specular_instances_binding, storage, trace->volumeBuffer(i));
		}
		if (ShaderCompiler::isRayQueryAvailable()) {
			const VoxelBlasCache* bricks = VulkanRenderer::instance->voxelBlasCache();
			writer.buffer(set, k_specular_trace_brick_bases_binding, storage, trace->brickBaseBuffer(i));
			writer.buffer(
			    set, k_specular_trace_bricks_binding, storage, bricks != nullptr ? bricks->poolBuffer() : trace->brickBaseBuffer(i)
			);
		}

		for (uint32_t parity = 0; parity < 2; ++parity) {
			auto image_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &image_layout));
			m_image_sets.push_back(std::move(image_set[0]));
			setDebugName(core, *m_image_sets.back(), std::format("TracedSpecular ImageSet[{}][{}]", i, parity));
		}
	}
	writer.flush(device);
	m_storage_bound.assign(m_storage_sets.size(), nullptr);
	m_image_bound.assign(m_image_sets.size(), {});
}

void TracedSpecularPass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	const vk::Extent2D half = PostProcessTarget::halfExtent(extent);
	m_target.create(core, extent, m_scene_format, "TracedSpecular");
	m_trace.create(core, half, k_trace_format, "TracedSpecular Trace");
	m_resolved.create(core, half, k_trace_format, "TracedSpecular Resolved");
	m_history[0].create(core, half, k_trace_format, "TracedSpecular History0");
	m_history[1].create(core, half, k_trace_format, "TracedSpecular History1");
	std::ranges::fill(m_image_bound, std::array<vk::ImageView, k_image_bindings> {});
	m_history_valid = false;
}

void TracedSpecularPass::onResize(vk::Extent2D extent) {
	if (m_core != nullptr && m_ready) {
		createTargets(*m_core, extent);
	}
}

auto TracedSpecularPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	auto* renderer = VulkanRenderer::instance;
	const auto* frame = renderer->renderingFrame();
	if (!m_ready || !renderer->tracedSpecularFrame() || !source_view || frame == nullptr || !frame->voxel_storage ||
	    !frame->voxel_storage->isReady() || frame_index >= m_storage_sets.size()) {
		return source_view;
	}

	// A repeated snapshot carries no new sample so the last output stands
	if (m_history_valid && frame->sequence == m_last_sequence) {
		return m_target.view();
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
	const vk::ImageView previous_view = renderer->previousResolvedColour();
	const std::array<vk::ImageView, k_image_bindings> views {
	  source_view,
	  renderer->getDepthView(),
	  renderer->getVoxelAlbedoView(),
	  renderer->getVoxelMaterialView(),
	  renderer->getVoxelFaceNormalView(),
	  renderer->getSceneNormalView(),
	  m_trace.view(),
	  history_read.view(),
	  renderer->getSceneMotionView(),
	  history_write.view(),
	  previous_view ? previous_view : source_view,
	  m_resolved.view(),
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
			    *m_point_sampler,
			    views[binding],
			    binding == k_specular_depth_binding ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal
			);
		}
		writer.flush(m_core->getDevice());
		m_image_bound[set_index] = views;
	}
	m_scene_sets.updateTlas(frame_index);

	const uint64_t sequence = frame->sequence;
	const bool history_valid = m_history_valid && sequence == m_last_sequence + 1;
	if (!history_valid) {
		history_read.makeSampleable(cmd);
	}

	Params params {};
	params.inverse_view_projection = glm::inverse(frame->frame_data.jittered_view_projection);
	params.tracing = glm::vec4(
	    renderer->voxelSpecularReach(),
	    renderer->voxelSpecularRoughness(),
	    history_valid ? 1.0f : 0.0f,
	    static_cast<float>(renderer->voxelSunReach())
	);
	params.resolve = glm::vec4(previous_view ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);

	const vk::PipelineLayout layout = *m_layout.getPipelineLayout();
	const std::array<vk::DescriptorSet, 3> sets {m_scene_sets.get(frame_index), *m_storage_sets[frame_index], image_set};
	const auto draw = [&](const PostProcessTarget& target, const VulkanPipeline& pipeline) {
		target.beginScope(cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, k_specular_scene_set, sets, {});
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
		cmd.draw(3, 1, 0, 0);
		target.endScope(cmd);
	};

	draw(m_trace, m_trace_pipeline);
	draw(m_resolved, m_resolve_pipeline);
	draw(history_write, m_temporal_pipeline);
	draw(m_target, m_composite_pipeline);

	m_parity = 1 - m_parity;
	m_history_valid = true;
	m_last_sequence = sequence;
	return m_target.view();
}

}
