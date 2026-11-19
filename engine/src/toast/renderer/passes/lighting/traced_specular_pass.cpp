/**
 * @file traced_specular_pass.cpp
 * @author dario
 * @date 25/10/2026
 */

#include "toast/renderer/passes/lighting/traced_specular_pass.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/shaders/shader_compiler.hpp"
#include "toast/renderer/voxel/voxel_blas_cache.hpp"
#include "toast/renderer/voxel/voxel_gpu_storage.hpp"
#include "toast/renderer/voxel/voxel_trace_scene.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <bit>

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
/// voxel_specular.slang gColourPyramid
constexpr uint32_t k_specular_pyramid_binding = 13;

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
	build_pipeline(m_surface_pipeline, k_surface_format, half, "surfaceMain", "TracedSpecular Surface");
	build_pipeline(m_trace_pipeline, k_trace_format, half, "traceMain", "TracedSpecular Trace");
	build_pipeline(m_resolve_pipeline, k_trace_format, half, "resolveMain", "TracedSpecular Resolve");
	build_pipeline(m_temporal_pipeline, k_trace_format, half, "temporalMain", "TracedSpecular Temporal");
	build_pipeline(m_composite_pipeline, scene_format, extent, "compositeMain", "TracedSpecular Composite");
	build_pipeline(m_pyramid_pipeline, k_pyramid_format, half, "pyramidMain", "TracedSpecular Pyramid");
	build_pipeline(m_filter_pipeline, k_trace_format, half, "filterMain", "TracedSpecular Filter");

	m_scene_sets.create(core, shader->reflection, *layouts[k_specular_scene_set], "TracedSpecular");
	m_instances_bound = std::ranges::any_of(shader->reflection.bindings, [](const ShaderBinding& binding) {
		return binding.set == k_specular_storage_set && binding.binding == k_specular_instances_binding;
	});
	createSets(core);
	createTargets(core, extent);

	m_ready = m_surface_pipeline.isReady() && m_trace_pipeline.isReady() && m_resolve_pipeline.isReady() &&
	          m_temporal_pipeline.isReady() && m_composite_pipeline.isReady() && m_pyramid_pipeline.isReady() &&
	          m_filter_pipeline.isReady() && !m_scene_sets.empty() && !m_storage_sets.empty();
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
	m_pyramid_sampler = vk::raii::Sampler(device, linearClampMippedSamplerInfo(static_cast<float>(k_pyramid_levels)));
	setDebugName(core, *m_pyramid_sampler, "TracedSpecular PyramidSampler");

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
	m_storage_bound.resize(m_storage_sets.size());
	m_image_bound.resize(m_image_sets.size());
}

void TracedSpecularPass::createTargets(const VulkanCore& core, vk::Extent2D extent) {
	const vk::Extent2D half = PostProcessTarget::halfExtent(extent);
	m_target.create(core, extent, m_scene_format, "TracedSpecular");
	m_surface.create(core, half, k_surface_format, "TracedSpecular Surface");
	m_trace.create(core, half, k_trace_format, "TracedSpecular Trace");
	m_resolved.create(core, half, k_trace_format, "TracedSpecular Resolved");
	m_history[0].create(core, half, k_trace_format, "TracedSpecular History0");
	m_history[1].create(core, half, k_trace_format, "TracedSpecular History1");
	m_filtered.create(core, half, k_trace_format, "TracedSpecular Filtered");
	createPyramid(core, half);
	m_image_bound.invalidate();
	m_history_valid = false;
}

void TracedSpecularPass::createPyramid(const VulkanCore& core, vk::Extent2D extent) {
	// Views before the image they view
	m_pyramid_top_view.reset();
	m_pyramid_view.reset();
	m_pyramid.reset();
	m_pyramid_extent = extent;
	m_pyramid_level_count =
	    std::min(k_pyramid_levels, static_cast<uint32_t>(std::bit_width(std::max(extent.width, extent.height))));

	auto image_ci =
	    colorTargetImageInfo(extent, k_pyramid_format, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	image_ci.mipLevels = m_pyramid_level_count;
	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_pyramid.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(core, **m_pyramid, "TracedSpecular Pyramid");
	const vk::ImageSubresourceRange levels(vk::ImageAspectFlagBits::eColor, 0, m_pyramid_level_count, 0, 1);
	m_pyramid_state.reset(**m_pyramid, levels);

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_pyramid;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = k_pyramid_format;
	view_ci.subresourceRange = levels;
	m_pyramid_view.emplace(core.getDevice(), view_ci);
	setDebugName(core, **m_pyramid_view, "TracedSpecular PyramidView");
	view_ci.subresourceRange = colorSubresourceRange();
	m_pyramid_top_view.emplace(core.getDevice(), view_ci);
	setDebugName(core, **m_pyramid_top_view, "TracedSpecular PyramidTopView");
}

void TracedSpecularPass::recordPyramid(
    vk::CommandBuffer cmd, vk::PipelineLayout layout, std::span<const vk::DescriptorSet> sets, const Params& params
) {
	ZoneScoped;
	sync::transition(cmd, m_pyramid_state, sync::Usage::color_attachment);
	vk::RenderingAttachmentInfo attachment {};
	attachment.imageView = **m_pyramid_top_view;
	attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
	attachment.storeOp = vk::AttachmentStoreOp::eStore;
	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, m_pyramid_extent);
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &attachment;
	cmd.beginRendering(rendering_info);
	const vk::Viewport viewport(
	    0.0f, 0.0f, static_cast<float>(m_pyramid_extent.width), static_cast<float>(m_pyramid_extent.height), 0.0f, 1.0f
	);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {vk::Rect2D({0, 0}, m_pyramid_extent)});
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pyramid_pipeline.getPipeline());
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, k_specular_scene_set, sets, {});
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
	cmd.draw(3, 1, 0, 0);
	cmd.endRendering();
	if (m_pyramid_level_count < 2) {
		sync::transition(cmd, m_pyramid_state, sync::Usage::shader_sampled);
		return;
	}

	const vk::Image image = **m_pyramid;
	const auto level = [](uint32_t mip, uint32_t count = 1) {
		return vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, mip, count, 0, 1);
	};
	const auto size = [&](uint32_t mip) {
		return vk::Offset3D {
		  static_cast<int32_t>(std::max(m_pyramid_extent.width >> mip, 1u)),
		  static_cast<int32_t>(std::max(m_pyramid_extent.height >> mip, 1u)),
		  1
		};
	};
	sync::BarrierBatch barriers;
	barriers.image(image, level(0), sync::Usage::color_attachment, sync::Usage::transfer_src);
	barriers.image(image, level(1, m_pyramid_level_count - 1), sync::Usage::color_attachment, sync::Usage::transfer_dst);
	barriers.flush(cmd);
	for (uint32_t mip = 1; mip < m_pyramid_level_count; ++mip) {
		vk::ImageBlit blit {};
		blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip - 1, 0, 1);
		blit.srcOffsets[1] = size(mip - 1);
		blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip, 0, 1);
		blit.dstOffsets[1] = size(mip);
		cmd.blitImage(
		    image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear
		);
		barriers.image(image, level(mip), sync::Usage::transfer_dst, sync::Usage::transfer_src);
		barriers.flush(cmd);
	}
	barriers.image(image, level(0, m_pyramid_level_count), sync::Usage::transfer_src, sync::Usage::shader_sampled);
	barriers.flush(cmd);
	m_pyramid_state.usage = sync::Usage::shader_sampled;
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
	if (m_storage_bound.update(frame_index, frame->voxel_storage)) {
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_storage_sets[frame_index], *frame->voxel_storage);
	}

	// Views only change on a resize after a wait for idle so recreating here never frees a pyramid in flight
	const vk::ImageView depth_view = renderer->getOpaqueDepthView();
	const vk::Extent2D depth_extent = renderer->renderExtent();
	if (depth_view && m_hiz.source() != depth_view) {
		m_hiz.create(*m_core, depth_view, depth_extent, HiZPyramid::Reduction::closest);
	}
	if (!m_hiz.isReady()) {
		return source_view;
	}

	const PostProcessTarget& history_write = m_history[m_parity];
	const PostProcessTarget& history_read = m_history[1 - m_parity];
	const uint32_t set_index = (frame_index * 2) + m_parity;
	const vk::DescriptorSet image_set = *m_image_sets[set_index];
	const vk::ImageView previous_view = renderer->previousResolvedColour();
	const std::array<vk::ImageView, k_image_bindings> views {
	  source_view,
	  renderer->getOpaqueDepthView(),
	  renderer->getVoxelAlbedoView(),
	  renderer->getVoxelMaterialView(),
	  renderer->getVoxelFaceNormalView(),
	  renderer->getOpaqueNormalView(),
	  m_trace.view(),
	  history_read.view(),
	  renderer->getSceneMotionView(),
	  history_write.view(),
	  previous_view ? previous_view : source_view,
	  m_resolved.view(),
	  m_surface.view(),
	  m_pyramid_view.has_value() ? **m_pyramid_view : vk::ImageView {},
	  m_hiz.view(),
	  m_filtered.view(),
	};
	if (std::ranges::any_of(views, [](vk::ImageView view) { return !view; })) {
		return source_view;
	}
	if (m_image_bound.update(set_index, views)) {
		DescriptorWriter writer;
		for (uint32_t binding = 0; binding < k_image_bindings; ++binding) {
			writer.image(
			    image_set,
			    binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    binding == k_specular_pyramid_binding ? *m_pyramid_sampler : *m_point_sampler,
			    views[binding],
			    binding == k_specular_depth_binding ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal
			);
		}
		writer.flush(m_core->getDevice());
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

	{
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "TracedSpecular depth pyramid");
		m_hiz.record(cmd, depth_extent, frame->frame_data.jittered_view_projection);
		sync::BarrierBatch barriers;
		m_hiz.beforeFragmentRead(barriers);
		barriers.flush(cmd);
	}
	const auto ssr = renderer->ssrSettings();
	params.march = glm::vec4(
	    static_cast<float>(m_hiz.builtLevels()),
	    static_cast<float>(ssr.max_steps),
	    ssr.thickness,
	    renderer->voxelSpecularHitLights() ? 1.0f : 0.0f
	);

	const vk::PipelineLayout layout = *m_layout.getPipelineLayout();
	const std::array<vk::DescriptorSet, 3> sets {m_scene_sets.get(frame_index), *m_storage_sets[frame_index], image_set};
	const auto draw = [&](const PostProcessTarget& target, const VulkanPipeline& pipeline, std::string_view scope_name) {
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, scope_name);
		target.beginScope(cmd);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, k_specular_scene_set, sets, {});
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(Params), &params);
		cmd.draw(3, 1, 0, 0);
		target.endScope(cmd);
	};

	if (previous_view) {
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "TracedSpecular colour pyramid");
		recordPyramid(cmd, layout, sets, params);
	} else if (m_pyramid_state.usage == sync::Usage::none) {
		sync::transition(cmd, m_pyramid_state, sync::Usage::shader_sampled);
	}
	draw(m_surface, m_surface_pipeline, "TracedSpecular surface");
	draw(m_trace, m_trace_pipeline, "TracedSpecular trace");
	draw(m_resolved, m_resolve_pipeline, "TracedSpecular resolve");
	draw(history_write, m_temporal_pipeline, "TracedSpecular temporal");
	draw(m_filtered, m_filter_pipeline, "TracedSpecular filter");
	draw(m_target, m_composite_pipeline, "TracedSpecular composite");

	m_parity = 1 - m_parity;
	m_history_valid = true;
	m_last_sequence = sequence;
	return m_target.view();
}

}
