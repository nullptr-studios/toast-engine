#include "voxel_pass.hpp"

#include "../descriptor_writer.hpp"
#include "../shader_cache.hpp"
#include "../voxel_gpu_storage.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

static_assert(sizeof(glm::mat4) == 64);

constexpr uint32_t k_scene_set = 0;
constexpr uint32_t k_storage_set = 1;
constexpr uint32_t k_instance_binding = 6;

/// Albedo normal material depth face normal at bindings 0 to 4 of voxel_lighting.slang
constexpr uint32_t k_gbuffer_set = 1;

constexpr uint32_t k_depth_binding = 3;

/// Turns the blur rotation each frame for TAA to average
constexpr float k_golden_ratio_conjugate = 0.61803398875f;

}

VoxelPass::VoxelPass(const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent)
    : m_core(&core) {
	ZoneScoped;
	static_assert(sizeof(PushConstants) == 16, "PushConstants is mirrored by voxel.slang");

	const auto uid = assets::resolveURI("core://shaders/voxel.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "VoxelPass shader core://shaders/voxel.slang unavailable; voxel volumes will not draw");
		setEnabled(false);
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "VoxelPass");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.color_format = k_voxel_albedo_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.extra_color_formats = voxelGbufferExtraColorFormats();
	config.write_extra_color = true;
	config.write_color_alpha = true;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.depth_test = true;
	config.depth_write = true;

	for (const Cull cull : {Cull::back, Cull::front}) {
		for (const Depth depth : {Depth::conservative, Depth::exact}) {
			config.cull_mode = cull == Cull::back ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eFront;
			config.fragment_entry = depth == Depth::conservative ? "fragmentOutside" : "fragmentInside";
			config.debug_name = std::format(
			    "VoxelPass {} {}", cull == Cull::back ? "CullBack" : "CullFront", depth == Depth::conservative ? "Outside" : "Inside"
			);
			m_pipelines[static_cast<size_t>(cull)][static_cast<size_t>(depth)].rebuild(core, config);
		}
	}

	createInstanceBuffers(core);
	createDescriptors(core, shader->reflection);
	createLighting(core, scene_format, extent);
	createNormalBlur(core, extent);
	TOAST_INFO("Render", "VoxelPass ready: dense DDA into a G-buffer, up to {} volumes a frame", k_max_instances);
}

void VoxelPass::createNormalBlur(const VulkanCore& core, vk::Extent2D extent) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/voxel_normal_blur.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "VoxelPass shader core://shaders/voxel_normal_blur.slang unavailable; voxel volumes will not draw");
		setEnabled(false);
		return;
	}

	m_blur_layout.rebuild(core, shader->reflection, "VoxelNormalBlur");
	const auto& layouts = m_blur_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "VoxelNormalBlur shader layout has no descriptor sets");
		setEnabled(false);
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "VoxelNormalBlur";
	config.color_format = k_scene_normal_format;
	config.write_color_alpha = true;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_blur_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	m_blur_pipeline.rebuild(core, config);

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout set_layout = *layouts[0];
	m_blur_sets.clear();
	m_bound_blur.assign(VulkanRenderer::k_frames_in_flight, {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto allocated = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
		m_blur_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_blur_sets.back(), std::format("VoxelNormalBlur Set[{}]", i));
	}
}

void VoxelPass::createLighting(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/voxel_lighting.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "VoxelPass shader core://shaders/voxel_lighting.slang unavailable; voxel volumes will not draw");
		setEnabled(false);
		return;
	}

	m_lighting_layout.rebuild(core, shader->reflection, "VoxelLighting");
	const auto& layouts = m_lighting_layout.getDescriptorSetLayouts();
	if (layouts.size() <= k_gbuffer_set) {
		TOAST_ERROR(
		    "Render",
		    "VoxelLighting shader layout declares {} descriptor sets; it needs the scene set and the G-buffer set",
		    layouts.size()
		);
		setEnabled(false);
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "VoxelLighting";
	config.color_format = scene_format;
	config.extra_color_formats = {k_scene_indirect_format};
	config.write_extra_color = true;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_lighting_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	m_lighting_pipeline.rebuild(core, config);

	m_lighting_scene_sets.create(core, shader->reflection, *layouts[k_scene_set], "VoxelLighting");

	// Nearest since every fetch is one texel of its own pixel
	m_point_sampler = vk::raii::Sampler(core.getDevice(), nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "VoxelLighting PointSampler");

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout gbuffer_layout = *layouts[k_gbuffer_set];
	m_gbuffer_sets.clear();
	m_bound_gbuffer.assign(VulkanRenderer::k_frames_in_flight, {});
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto allocated = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &gbuffer_layout));
		m_gbuffer_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_gbuffer_sets.back(), std::format("VoxelLighting GbufferSet[{}]", i));
	}
}

void VoxelPass::createInstanceBuffers(const VulkanCore& core) {
	ZoneScoped;
	m_instance_buffers.clear();
	m_instance_buffers.reserve(VulkanRenderer::k_frames_in_flight);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(VoxelInstanceGpu) * k_max_instances;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		m_instance_buffers.push_back(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(core, *m_instance_buffers.back(), std::format("VoxelPass Instances[{}]", i));
	}
}

void VoxelPass::createDescriptors(const VulkanCore& core, const ShaderReflection& reflection) {
	ZoneScoped;
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.size() <= k_storage_set) {
		TOAST_ERROR(
		    "Render",
		    "VoxelPass shader layout declares {} descriptor sets; it needs the engine scene set and the voxel storage set",
		    layouts.size()
		);
		setEnabled(false);
		return;
	}

	m_scene_sets.create(core, reflection, *layouts[k_scene_set], "VoxelPass");

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const auto& device = core.getDevice();

	m_storage_sets.clear();
	m_bound_storage.assign(VulkanRenderer::k_frames_in_flight, nullptr);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetLayout storage_layout = *layouts[k_storage_set];
		auto storage = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &storage_layout));
		m_storage_sets.push_back(std::move(storage[0]));
		setDebugName(core, *m_storage_sets.back(), std::format("VoxelPass StorageSet[{}]", i));

		const vk::DescriptorBufferInfo instance_info(*m_instance_buffers[i], 0, sizeof(VoxelInstanceGpu) * k_max_instances);
		const vk::WriteDescriptorSet write(
		    *m_storage_sets[i], k_instance_binding, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &instance_info
		);
		device.updateDescriptorSets(write, {});
	}
}

void VoxelPass::bindStorage(uint32_t frame_index, const std::shared_ptr<const VoxelGpuStorage>& storage) {
	ZoneScoped;
	// Safe since the fence of this slot was waited on
	writeVoxelStorageDescriptors(m_core->getDevice(), *m_storage_sets[frame_index], *storage);
	m_bound_storage[frame_index] = storage;
}

auto VoxelPass::prepare(uint32_t frame_index) -> bool {
	ZoneScoped;
	m_draws.clear();

	if (frame_index >= m_storage_sets.size() || frame_index >= m_instance_buffers.size() || !m_scene_sets.get(frame_index) ||
	    !m_lighting_pipeline.isReady() || !m_blur_pipeline.isReady()) {
		return false;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->voxel_instances.empty() || !frame->voxel_storage || !frame->voxel_storage->isReady()) {
		return false;
	}

	if (m_bound_storage[frame_index] != frame->voxel_storage) {
		bindStorage(frame_index, frame->voxel_storage);
	}

	const auto& allocation = m_instance_buffers[frame_index].getAllocation();
	auto* instances = static_cast<VoxelInstanceGpu*>(allocation.getInfo().pMappedData);
	if (instances == nullptr) {
		return false;
	}

	for (const auto& proxy : frame->voxel_instances) {
		if (!proxy.visible) {
			continue;
		}
		if (m_draws.size() >= k_max_instances) {
			break;
		}

		const auto index = static_cast<uint32_t>(m_draws.size());
		instances[index] =
		    makeVoxelInstance(proxy.model, proxy.inverse_model, proxy.previous_model, proxy.has_previous, proxy.record_index);
		instances[index].pad0 = proxy.highlight_record;
		m_draws.push_back(
		    Draw {
		      .instance = index,
		      .cull = proxy.camera_inside != proxy.mirrored ? Cull::front : Cull::back,
		      .depth = proxy.camera_inside ? Depth::exact : Depth::conservative,
		    }
		);
	}
	if (m_draws.empty()) {
		return false;
	}
	allocation.flush(0, sizeof(VoxelInstanceGpu) * m_draws.size());
	return true;
}

void VoxelPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;

	if (m_draws.empty()) {
		return;
	}

	const vk::PipelineLayout layout = *m_shader_layout.getPipelineLayout();
	const VulkanPipeline* bound = nullptr;
	m_scene_sets.updateTlas(frame_index);

	for (const Draw& draw : m_draws) {
		const VulkanPipeline& pipeline = m_pipelines[static_cast<size_t>(draw.cull)][static_cast<size_t>(draw.depth)];
		if (!pipeline.isReady()) {
			continue;
		}
		if (bound != &pipeline) {
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
			if (bound == nullptr) {
				cmd.bindDescriptorSets(
				    vk::PipelineBindPoint::eGraphics,
				    layout,
				    k_scene_set,
				    std::array<vk::DescriptorSet, 2> {m_scene_sets.get(frame_index), *m_storage_sets[frame_index]},
				    {}
				);
			}
			bound = &pipeline;
		}

		const PushConstants push {.instance_index = draw.instance};
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &push);
		cmd.draw(36, 1, 0, 0);
	}
}

void VoxelPass::recordLighting(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	if (!m_lighting_pipeline.isReady() || frame_index >= m_gbuffer_sets.size() || !m_lighting_scene_sets.get(frame_index)) {
		return;
	}

	const auto* instance = VulkanRenderer::instance;
	const auto* frame = instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	const std::array<vk::ImageView, 5> views {
	  instance->getVoxelAlbedoView(),
	  instance->getSceneNormalView(),
	  instance->getVoxelMaterialView(),
	  instance->getDepthView(),
	  instance->getVoxelFaceNormalView(),
	};
	if (std::ranges::any_of(views, [](vk::ImageView view) { return !view; })) {
		return;
	}

	// Safe since the fence of this slot was waited on
	if (m_bound_gbuffer[frame_index] != views) {
		DescriptorWriter writer;
		for (uint32_t binding = 0; binding < views.size(); ++binding) {
			writer.image(
			    *m_gbuffer_sets[frame_index],
			    binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    *m_point_sampler,
			    views[binding],
			    binding == k_depth_binding ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal
			);
		}
		writer.flush(m_core->getDevice());
		m_bound_gbuffer[frame_index] = views;
	}

	m_lighting_scene_sets.updateTlas(frame_index);

	const LightingPushConstants push {.inverse_view_projection = glm::inverse(frame->frame_data.jittered_view_projection)};
	const vk::PipelineLayout layout = *m_lighting_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_lighting_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_scene_set,
	    std::array<vk::DescriptorSet, 2> {m_lighting_scene_sets.get(frame_index), *m_gbuffer_sets[frame_index]},
	    {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(LightingPushConstants), &push);
	cmd.draw(3, 1, 0, 0);
}

void VoxelPass::recordNormalBlur(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D viewport) {
	ZoneScoped;
	if (!m_blur_pipeline.isReady() || frame_index >= m_blur_sets.size()) {
		return;
	}

	const auto* instance = VulkanRenderer::instance;
	const auto* frame = instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	const std::array<vk::ImageView, 3> views {
	  instance->getVoxelAlbedoView(), instance->getVoxelFaceNormalView(), instance->getDepthView()
	};
	if (std::ranges::any_of(views, [](vk::ImageView view) { return !view; })) {
		return;
	}

	// Safe since the fence of this slot was waited on
	if (m_bound_blur[frame_index] != views) {
		DescriptorWriter writer;
		for (uint32_t binding = 0; binding < views.size(); ++binding) {
			const bool depth = binding == views.size() - 1;
			writer.image(
			    *m_blur_sets[frame_index],
			    binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    *m_point_sampler,
			    views[binding],
			    depth ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal
			);
		}
		writer.flush(m_core->getDevice());
		m_bound_blur[frame_index] = views;
	}

	const auto height = static_cast<float>(viewport.height);
	const float rotation =
	    frame->taa_active ? std::fmod(static_cast<float>(frame->sequence % 4096) * k_golden_ratio_conjugate, 1.0f) : 0.0f;
	const NormalBlurParams params {
	  .inverse_view_projection = glm::inverse(frame->frame_data.jittered_view_projection),
	  .camera_radius = glm::vec4(frame->frame_data.camera_position, instance->voxelNormalRounding() * voxel::k_voxel_size),
	  .viewport = glm::vec4(
	      static_cast<float>(viewport.width), height, 0.5f * height * std::abs(frame->frame_data.projection[1][1]), rotation
	  ),
	};

	const vk::PipelineLayout layout = *m_blur_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_blur_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics, layout, 0, std::array<vk::DescriptorSet, 1> {*m_blur_sets[frame_index]}, {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(NormalBlurParams), &params);
	cmd.draw(3, 1, 0, 0);
}

}
