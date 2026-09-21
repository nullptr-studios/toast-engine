#include "voxel_pass.hpp"

#include "../shader_cache.hpp"
#include "../voxel_gpu_storage.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <array>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

static_assert(sizeof(glm::mat4) == 64);

constexpr uint32_t k_scene_set = 0;
constexpr uint32_t k_storage_set = 1;
constexpr uint32_t k_instance_binding = 6;

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
	config.color_format = scene_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.extra_color_formats = worldStageExtraColorFormats();
	config.write_extra_color = true;
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
	TOAST_INFO("Render", "VoxelPass ready: dense DDA, up to {} volumes a frame", k_max_instances);
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

void VoxelPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;

	if (frame_index >= m_storage_sets.size() || frame_index >= m_instance_buffers.size() || !m_scene_sets.get(frame_index)) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->voxel_instances.empty() || !frame->voxel_storage || !frame->voxel_storage->isReady()) {
		return;
	}

	if (m_bound_storage[frame_index] != frame->voxel_storage) {
		bindStorage(frame_index, frame->voxel_storage);
	}

	const auto& allocation = m_instance_buffers[frame_index].getAllocation();
	auto* instances = static_cast<VoxelInstanceGpu*>(allocation.getInfo().pMappedData);
	if (instances == nullptr) {
		return;
	}

	m_draws.clear();
	for (const auto& proxy : frame->voxel_instances) {
		if (!proxy.visible) {
			continue;
		}
		if (m_draws.size() >= k_max_instances) {
			break;
		}

		const auto index = static_cast<uint32_t>(m_draws.size());
		instances[index] = makeVoxelInstance(proxy.model, proxy.inverse_model, proxy.record_index);
		m_draws.push_back(
		    Draw {
		      .instance = index,
		      .cull = proxy.camera_inside != proxy.mirrored ? Cull::front : Cull::back,
		      .depth = proxy.camera_inside ? Depth::exact : Depth::conservative,
		    }
		);
	}
	if (m_draws.empty()) {
		return;
	}
	allocation.flush(0, sizeof(VoxelInstanceGpu) * m_draws.size());

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

}
