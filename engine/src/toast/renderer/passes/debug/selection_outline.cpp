/**
 * @file selection_outline.cpp
 * @author dario
 * @date 16/10/2026
 */

#include "toast/renderer/passes/debug/selection_outline.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/resources/vulkan_mesh.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/voxel/voxel_gpu_storage.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_silhouette_bit = 1u;
constexpr uint32_t k_visible_bit = 2u;

/// Matches kWidth in selection_outline.slang plus a pixel
constexpr int32_t k_outline_reach = 3;

const glm::vec4 k_solid_color {1.0f, 0.55f, 0.1f, 1.0f};
const glm::vec4 k_hidden_color {0.55f, 0.55f, 0.55f, 1.0f};

/// Weight of the newest frame in the outline history
constexpr float k_history_blend = 0.1f;

constexpr vk::Format k_coverage_format = vk::Format::eR8G8Unorm;

auto selectMaskFormat(const VulkanCore& core) -> vk::Format {
	constexpr std::array candidates {vk::Format::eS8Uint, vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint};
	constexpr vk::FormatFeatureFlags needed =
	    vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage;
	for (const auto candidate : candidates) {
		const auto props = core.getPhysicalDevice().getFormatProperties(candidate);
		if ((props.optimalTilingFeatures & needed) == needed) {
			return candidate;
		}
	}
	return vk::Format::eUndefined;
}

auto maskAspects(vk::Format format) -> vk::ImageAspectFlags {
	return format == vk::Format::eS8Uint ? vk::ImageAspectFlags {vk::ImageAspectFlagBits::eStencil}
	                                     : vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
}

/// Pixels a world sphere covers or the whole extent once it reaches behind the camera
auto sphereRect(const glm::mat4& view_projection, glm::vec3 center, float radius, vk::Extent2D extent) -> vk::Rect2D {
	const vk::Rect2D full {
	  {0, 0},
    extent
	};
	if (radius <= 0.0f) {
		return full;
	}

	glm::vec2 low(std::numeric_limits<float>::max());
	glm::vec2 high(std::numeric_limits<float>::lowest());
	for (uint32_t corner = 0; corner < 8; ++corner) {
		const glm::vec3 sign(
		    (corner & 1u) != 0u ? 1.0f : -1.0f, (corner & 2u) != 0u ? 1.0f : -1.0f, (corner & 4u) != 0u ? 1.0f : -1.0f
		);
		const glm::vec4 clip = view_projection * glm::vec4(center + sign * radius, 1.0f);
		if (clip.w <= 1e-4f) {
			return full;
		}
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		low = glm::min(low, ndc);
		high = glm::max(high, ndc);
	}

	const glm::vec2 size(static_cast<float>(extent.width), static_cast<float>(extent.height));
	const glm::ivec2 begin = glm::ivec2(glm::floor((low * 0.5f + 0.5f) * size)) - k_outline_reach;
	const glm::ivec2 end = glm::ivec2(glm::ceil((high * 0.5f + 0.5f) * size)) + k_outline_reach;
	const glm::ivec2 clamped_begin = glm::clamp(begin, glm::ivec2(0), glm::ivec2(size));
	const glm::ivec2 clamped_end = glm::clamp(end, glm::ivec2(0), glm::ivec2(size));
	return {
	  {                                       clamped_begin.x,                                        clamped_begin.y},
	  {static_cast<uint32_t>(clamped_end.x - clamped_begin.x), static_cast<uint32_t>(clamped_end.y - clamped_begin.y)}
	};
}

auto unite(const vk::Rect2D& a, const vk::Rect2D& b) -> vk::Rect2D {
	if (a.extent.width == 0 || a.extent.height == 0) {
		return b;
	}
	if (b.extent.width == 0 || b.extent.height == 0) {
		return a;
	}
	const int32_t x0 = std::min(a.offset.x, b.offset.x);
	const int32_t y0 = std::min(a.offset.y, b.offset.y);
	const int32_t x1 =
	    std::max(a.offset.x + static_cast<int32_t>(a.extent.width), b.offset.x + static_cast<int32_t>(b.extent.width));
	const int32_t y1 =
	    std::max(a.offset.y + static_cast<int32_t>(a.extent.height), b.offset.y + static_cast<int32_t>(b.extent.height));
	return {
	  {                            x0,                             y0},
    {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}
	};
}

auto outlinedMesh(const VulkanRenderer::MeshInstanceProxy& proxy) -> bool {
	return proxy.outlined && proxy.visible && proxy.mesh != nullptr && proxy.mesh->isReady();
}

}

SelectionOutline::SelectionOutline(const VulkanCore& core, vk::Format output_format, vk::Format depth_format, vk::Extent2D extent)
    : m_core(&core) {
	ZoneScoped;
	m_mask_format = selectMaskFormat(core);
	if (m_mask_format == vk::Format::eUndefined) {
		TOAST_ERROR("Render", "No sampleable stencil format so selected nodes are not outlined");
		return;
	}

	createOutlinePipelines(output_format, depth_format, extent);
	createMaskPipelines(extent);
	createTargets(extent);
}

void SelectionOutline::onResize(vk::Extent2D extent) {
	if (m_mask_format != vk::Format::eUndefined) {
		createTargets(extent);
	}
}

void SelectionOutline::createTargets(vk::Extent2D extent) {
	ZoneScoped;
	m_mask_sampled.reset();
	m_mask_attachment.reset();
	m_mask.reset();
	m_extent = extent;
	m_drawn = false;
	m_history_valid = false;
	std::ranges::fill(m_bound_outline, std::array<vk::ImageView, 3> {});
	if (extent.width == 0 || extent.height == 0) {
		return;
	}

	m_coverage[0].create(*m_core, extent, k_coverage_format, "SelectionOutline Coverage[0]");
	m_coverage[1].create(*m_core, extent, k_coverage_format, "SelectionOutline Coverage[1]");

	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = m_mask_format;
	image_ci.extent = vk::Extent3D {extent.width, extent.height, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 1;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_mask.emplace(m_core->getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(*m_core, **m_mask, "SelectionOutline Mask");

	const vk::ImageSubresourceRange range {maskAspects(m_mask_format), 0, 1, 0, 1};
	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_mask;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = m_mask_format;
	view_ci.subresourceRange = range;
	m_mask_attachment.emplace(m_core->getDevice(), view_ci);
	setDebugName(*m_core, **m_mask_attachment, "SelectionOutline MaskAttachment");

	view_ci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
	m_mask_sampled.emplace(m_core->getDevice(), view_ci);
	setDebugName(*m_core, **m_mask_sampled, "SelectionOutline MaskStencil");

	m_mask_state.reset(**m_mask, range);
}

void SelectionOutline::createMaskPipelines(vk::Extent2D extent) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/selection_mask.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "core://shaders/selection_mask.slang unavailable so selected nodes are not outlined");
		return;
	}

	m_mask_layout.rebuild(*m_core, shader->reflection, "SelectionMask");
	const auto& layouts = m_mask_layout.getDescriptorSetLayouts();
	if (layouts.size() < 2) {
		TOAST_ERROR("Render", "SelectionMask needs the frame set and the voxel storage set");
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.depth_only = true;
	config.depth_fragment = true;
	config.stencil_format = m_mask_format;
	config.stencil_replace = true;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_mask_layout.getPipelineLayout();

	VulkanPipeline::Config mesh = config;
	mesh.debug_name = "SelectionMask Mesh";
	mesh.vertex_entry = "meshVertex";
	mesh.fragment_entry = "meshFragment";
	mesh.vertex_bindings = {vertexBindingDescription()};
	const auto vertex_attributes = vertexAttributeDescriptions();
	mesh.vertex_attributes.assign(vertex_attributes.begin(), vertex_attributes.end());
	// Mirrored and single sided meshes still cover their silhouette
	mesh.cull_mode = vk::CullModeFlagBits::eNone;
	m_mesh_pipeline.rebuild(*m_core, mesh);

	VulkanPipeline::Config voxel = config;
	voxel.debug_name = "SelectionMask Voxels";
	voxel.vertex_entry = "voxelVertex";
	voxel.fragment_entry = "voxelFragment";
	voxel.cull_mode = vk::CullModeFlagBits::eFront;
	m_voxel_pipeline.rebuild(*m_core, voxel);

	const auto& device = m_core->getDevice();
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout frame_layout = *layouts[0];
	const vk::DescriptorSetLayout voxel_layout = *layouts[1];

	m_mask_sets.clear();
	m_voxel_sets.clear();
	m_volume_buffers.clear();
	m_bound_depth.assign(VulkanRenderer::k_frames_in_flight, vk::ImageView {});
	m_bound_storage.assign(VulkanRenderer::k_frames_in_flight, nullptr);

	DescriptorWriter writer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(VoxelInstanceGpu) * k_max_volumes;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		m_volume_buffers.push_back(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(*m_core, *m_volume_buffers.back(), std::format("SelectionMask Volumes[{}]", i));

		auto frame_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &frame_layout));
		m_mask_sets.push_back(std::move(frame_set[0]));
		setDebugName(*m_core, *m_mask_sets.back(), std::format("SelectionMask FrameSet[{}]", i));

		auto voxel_set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &voxel_layout));
		m_voxel_sets.push_back(std::move(voxel_set[0]));
		setDebugName(*m_core, *m_voxel_sets.back(), std::format("SelectionMask VoxelSet[{}]", i));

		if (const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		    frame_res != nullptr && frame_res->gpu_buffer.has_value()) {
			writer.buffer(
			    *m_mask_sets[i], 0, vk::DescriptorType::eUniformBuffer, **frame_res->gpu_buffer, sizeof(VulkanRenderer::FrameUBO)
			);
		}
		writer.buffer(
		    *m_mask_sets[i],
		    13,
		    vk::DescriptorType::eStorageBuffer,
		    VulkanRenderer::instance->getInstanceBuffer(i),
		    sizeof(VulkanRenderer::InstanceData) * VulkanRenderer::k_max_instances
		);
		writer.buffer(
		    *m_voxel_sets[i], 6, vk::DescriptorType::eStorageBuffer, *m_volume_buffers[i], sizeof(VoxelInstanceGpu) * k_max_volumes
		);
	}
	writer.flush(device);

	if (!m_mesh_pipeline.isReady() || !m_voxel_pipeline.isReady()) {
		TOAST_ERROR("Render", "SelectionMask pipelines failed to build so selected nodes are not outlined");
	}
}

void SelectionOutline::createOutlinePipelines(vk::Format output_format, vk::Format depth_format, vk::Extent2D extent) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/selection_outline.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "core://shaders/selection_outline.slang unavailable so selected nodes are not outlined");
		return;
	}

	m_outline_layout.rebuild(*m_core, shader->reflection, "SelectionOutline");
	const auto& layouts = m_outline_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_outline_layout.getPipelineLayout();
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;

	VulkanPipeline::Config resolve = config;
	resolve.debug_name = "SelectionOutline Resolve";
	resolve.color_format = k_coverage_format;
	resolve.write_color_alpha = true;
	resolve.fragment_entry = "resolveMain";
	m_resolve_pipeline.rebuild(*m_core, resolve);

	VulkanPipeline::Config composite = config;
	composite.debug_name = "SelectionOutline Composite";
	composite.color_format = output_format;
	// The output scope keeps the scene depth bound
	composite.depth_format = depth_format;
	composite.fragment_entry = "compositeMain";
	composite.blend_preset = VulkanPipeline::BlendPreset::premultiplied;
	m_composite_pipeline.rebuild(*m_core, composite);

	const auto& device = m_core->getDevice();
	// Integer stencil texels never filter
	m_sampler.emplace(device, nearestClampSamplerInfo());
	setDebugName(*m_core, **m_sampler, "SelectionOutline Sampler");

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout set_layout = *layouts[0];
	m_outline_sets.clear();
	m_bound_outline.assign(VulkanRenderer::k_frames_in_flight, {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto allocated = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
		m_outline_sets.push_back(std::move(allocated[0]));
		setDebugName(*m_core, *m_outline_sets.back(), std::format("SelectionOutline Set[{}]", i));
	}
}

auto SelectionOutline::prepareVolumes(uint32_t frame_index) -> uint32_t {
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const auto& storage = frame->voxel_storage;
	if (!storage || !storage->isReady() || frame_index >= m_volume_buffers.size()) {
		return 0;
	}

	auto* instances = static_cast<VoxelInstanceGpu*>(m_volume_buffers[frame_index].getAllocation().getInfo().pMappedData);
	if (instances == nullptr) {
		return 0;
	}

	uint32_t count = 0;
	for (const auto& proxy : frame->voxel_instances) {
		if (!proxy.outlined || !proxy.visible) {
			continue;
		}
		if (count == k_max_volumes) {
			break;
		}
		instances[count] = makeVoxelInstance(proxy.model, proxy.inverse_model, proxy.model, true, proxy.record_index);
		if (proxy.mirrored) {
			instances[count].flags |= k_voxel_instance_mirrored;
		}
		++count;
	}
	if (count == 0) {
		return 0;
	}
	m_volume_buffers[frame_index].getAllocation().flush(0, sizeof(VoxelInstanceGpu) * count);

	if (m_bound_storage[frame_index] != storage) {
		// Safe since the fence of this slot was waited on
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_voxel_sets[frame_index], *storage);
		m_bound_storage[frame_index] = storage;
	}
	return count;
}

void SelectionOutline::recordMask(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView scene_depth) {
	ZoneScoped;
	m_drawn = false;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || !m_mask_attachment.has_value() || !m_mesh_pipeline.isReady() || !m_voxel_pipeline.isReady() ||
	    !m_resolve_pipeline.isReady() || !m_composite_pipeline.isReady() || !scene_depth || frame_index >= m_mask_sets.size() ||
	    frame_index >= m_outline_sets.size()) {
		m_history_valid = false;
		return;
	}

	const glm::mat4& view_projection = frame->frame_data.view_projection;
	vk::Rect2D rect {};
	bool any_mesh = false;
	for (const auto& proxy : frame->mesh_instances) {
		if (outlinedMesh(proxy)) {
			any_mesh = true;
			rect = unite(rect, sphereRect(view_projection, proxy.bounds_center, proxy.bounds_radius, m_extent));
		}
	}
	const uint32_t volumes = prepareVolumes(frame_index);
	if (volumes > 0) {
		for (const auto& proxy : frame->voxel_instances) {
			if (proxy.outlined && proxy.visible) {
				rect = unite(rect, sphereRect(view_projection, proxy.bounds_center, proxy.bounds_radius, m_extent));
			}
		}
	}
	if ((!any_mesh && volumes == 0) || rect.extent.width == 0 || rect.extent.height == 0) {
		m_history_valid = false;
		return;
	}

	// The history only lines up while nothing it shows has moved
	std::vector<glm::mat4> models;
	for (const auto& proxy : frame->mesh_instances) {
		if (outlinedMesh(proxy)) {
			models.push_back(proxy.model);
		}
	}
	for (const auto& proxy : frame->voxel_instances) {
		if (proxy.outlined && proxy.visible) {
			models.push_back(proxy.model);
		}
	}
	const bool reset = !m_history_valid || !frame->taa_active || frame->frame_data.view_projection != m_history_view_projection ||
	                   models != m_history_models;
	m_history_view_projection = frame->frame_data.view_projection;
	m_history_models = std::move(models);

	if (m_bound_depth[frame_index] != scene_depth) {
		DescriptorWriter writer;
		writer.image(
		    *m_mask_sets[frame_index],
		    1,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_sampler,
		    scene_depth,
		    vk::ImageLayout::eDepthReadOnlyOptimal
		);
		writer.flush(m_core->getDevice());
		m_bound_depth[frame_index] = scene_depth;
	}

	sync::transition(cmd, m_mask_state, sync::Usage::stencil_attachment);

	vk::RenderingAttachmentInfo stencil_info {};
	stencil_info.imageView = **m_mask_attachment;
	stencil_info.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	stencil_info.loadOp = vk::AttachmentLoadOp::eClear;
	stencil_info.storeOp = vk::AttachmentStoreOp::eStore;
	stencil_info.clearValue = vk::ClearValue(vk::ClearDepthStencilValue {0.0f, 0});

	const vk::Rect2D full {
	  {0, 0},
    m_extent
	};
	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = full;
	rendering_info.layerCount = 1;
	rendering_info.pStencilAttachment = &stencil_info;

	cmd.beginRendering(rendering_info);
	cmd.setViewport(
	    0,
	    std::array {vk::Viewport(0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f)}
	);
	cmd.setScissor(0, std::array {full});

	const vk::PipelineLayout layout = *m_mask_layout.getPipelineLayout();
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, std::array {*m_mask_sets[frame_index]}, {});
	if (volumes > 0) {
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, std::array {*m_voxel_sets[frame_index]}, {});
	}

	const vk::Buffer posed_vertices = VulkanRenderer::instance->getPosedVertexBuffer(frame_index);
	for (const uint32_t bit : {k_silhouette_bit, k_visible_bit}) {
		cmd.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, bit);
		cmd.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack, bit);
		MaskPush push {.test_occlusion = bit == k_visible_bit ? 1u : 0u};

		if (any_mesh) {
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_mesh_pipeline.getPipeline());
			for (const auto& proxy : frame->mesh_instances) {
				if (!outlinedMesh(proxy)) {
					continue;
				}
				const bool posed = proxy.posed_vertex_offset != VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices && posed_vertices;
				if (posed) {
					proxy.mesh->bindPosed(cmd, posed_vertices, proxy.posed_vertex_offset);
				} else {
					proxy.mesh->bind(cmd);
				}
				push.instance = proxy.instance_index;
				cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(MaskPush), &push);
				proxy.mesh->draw(cmd, 1);
			}
		}

		if (volumes > 0) {
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_voxel_pipeline.getPipeline());
			for (uint32_t i = 0; i < volumes; ++i) {
				push.instance = i;
				cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(MaskPush), &push);
				cmd.draw(36, 1, 0, 0);
			}
		}
	}

	cmd.endRendering();
	sync::transition(cmd, m_mask_state, sync::Usage::fragment_sampled);

	m_outline_rect = rect;
	recordResolve(cmd, frame_index, reset);
	m_history_valid = true;
	m_drawn = true;
}

void SelectionOutline::recordResolve(vk::CommandBuffer cmd, uint32_t frame_index, bool reset) {
	m_write ^= 1u;
	const PostProcessTarget& target = m_coverage[m_write];
	const PostProcessTarget& history = m_coverage[m_write ^ 1u];
	if (reset) {
		// Only for a legal layout since a reset never weighs the history
		history.beginScope(cmd);
		history.endScope(cmd);
	}

	const std::array<vk::ImageView, 3> wanted {**m_mask_sampled, history.view(), target.view()};
	if (m_bound_outline[frame_index] != wanted) {
		DescriptorWriter writer;
		const vk::DescriptorSet set = *m_outline_sets[frame_index];
		const auto type = vk::DescriptorType::eCombinedImageSampler;
		writer.image(set, 0, type, *m_sampler, wanted[0], vk::ImageLayout::eShaderReadOnlyOptimal);
		writer.image(set, 1, type, *m_sampler, wanted[1], vk::ImageLayout::eShaderReadOnlyOptimal);
		writer.image(set, 2, type, *m_sampler, wanted[2], vk::ImageLayout::eShaderReadOnlyOptimal);
		writer.flush(m_core->getDevice());
		m_bound_outline[frame_index] = wanted;
	}

	const OutlinePush push {
	  .solid_color = k_solid_color,
	  .hidden_color = k_hidden_color,
	  .rect =
	      {m_outline_rect.offset.x,
				       m_outline_rect.offset.y,
				       m_outline_rect.offset.x + static_cast<int32_t>(m_outline_rect.extent.width),
				       m_outline_rect.offset.y + static_cast<int32_t>(m_outline_rect.extent.height)},
	  .blend = reset ? 1.0f : k_history_blend,
	};

	const vk::PipelineLayout layout = *m_outline_layout.getPipelineLayout();
	target.beginScope(cmd);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_resolve_pipeline.getPipeline());
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, std::array {*m_outline_sets[frame_index]}, {});
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(OutlinePush), &push);
	cmd.draw(3, 1, 0, 0);
	target.endScope(cmd);
}

void SelectionOutline::recordOutline(vk::CommandBuffer cmd, uint32_t frame_index) {
	if (!m_drawn || frame_index >= m_outline_sets.size()) {
		return;
	}

	const vk::PipelineLayout layout = *m_outline_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_composite_pipeline.getPipeline());
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, std::array {*m_outline_sets[frame_index]}, {});
	const OutlinePush push {.solid_color = k_solid_color, .hidden_color = k_hidden_color};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(OutlinePush), &push);
	cmd.setScissor(0, std::array {m_outline_rect});
	cmd.draw(3, 1, 0, 0);
	cmd.setScissor(
	    0,
	    std::array {
	      vk::Rect2D {{0, 0}, m_extent}
	}
	);
}

}
