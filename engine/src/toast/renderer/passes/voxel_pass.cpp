#include "voxel_pass.hpp"

#include "../shader_cache.hpp"
#include "../voxel_gpu_storage.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <array>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

static_assert(sizeof(glm::mat4) == 64);

constexpr uint32_t k_camera_set = 0;
constexpr uint32_t k_scene_set = 1;
constexpr uint32_t k_instance_binding = 6;

}

VoxelPass::VoxelPass(const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent)
    : m_core(&core) {
	ZoneScoped;
	static_assert(sizeof(InstanceGpu) == 144, "InstanceGpu is mirrored by voxel_dda.slang's VoxelInstance");
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
	createDescriptors(core);
	TOAST_INFO("Render", "VoxelPass ready: dense DDA, up to {} volumes a frame", k_max_instances);
}

void VoxelPass::createInstanceBuffers(const VulkanCore& core) {
	ZoneScoped;
	m_instance_buffers.clear();
	m_instance_buffers.reserve(VulkanRenderer::k_frames_in_flight);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(InstanceGpu) * k_max_instances;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		m_instance_buffers.push_back(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(core, *m_instance_buffers.back(), std::format("VoxelPass Instances[{}]", i));
	}
}

void VoxelPass::createDescriptors(const VulkanCore& core) {
	ZoneScoped;
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.size() <= k_scene_set) {
		TOAST_ERROR(
		    "Render", "VoxelPass shader layout declares {} descriptor sets; it needs the camera's and the scene's", layouts.size()
		);
		setEnabled(false);
		return;
	}

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const auto& device = core.getDevice();

	m_camera_sets.clear();
	m_scene_sets.clear();
	m_bound_storage.assign(VulkanRenderer::k_frames_in_flight, nullptr);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetLayout camera_layout = *layouts[k_camera_set];
		auto camera = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &camera_layout));
		m_camera_sets.push_back(std::move(camera[0]));
		setDebugName(core, *m_camera_sets.back(), std::format("VoxelPass CameraSet[{}]", i));

		const vk::DescriptorSetLayout scene_layout = *layouts[k_scene_set];
		auto scene = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &scene_layout));
		m_scene_sets.push_back(std::move(scene[0]));
		setDebugName(core, *m_scene_sets.back(), std::format("VoxelPass SceneSet[{}]", i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			TOAST_CRITICAL("Render", "Frame UBO buffer missing for frame {}", i);
			continue;
		}

		const vk::DescriptorBufferInfo camera_info(**frame_res->gpu_buffer, 0, sizeof(VulkanRenderer::FrameUBO));
		const vk::DescriptorBufferInfo instance_info(*m_instance_buffers[i], 0, sizeof(InstanceGpu) * k_max_instances);
		const std::array writes {
		  vk::WriteDescriptorSet(*m_camera_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &camera_info),
		  vk::WriteDescriptorSet(
		      *m_scene_sets[i], k_instance_binding, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &instance_info
		  ),
		};
		device.updateDescriptorSets(writes, {});
	}
}

void VoxelPass::bindStorage(uint32_t frame_index, const std::shared_ptr<const VoxelGpuStorage>& storage) {
	ZoneScoped;
	using Section = VoxelGpuStorage::Section;
	constexpr std::array k_sections {
	  Section::materials, Section::occupancy, Section::grids, Section::coarse, Section::palettes, Section::records
	};

	std::array<vk::DescriptorBufferInfo, VoxelGpuStorage::k_section_count> infos {};
	std::array<vk::WriteDescriptorSet, VoxelGpuStorage::k_section_count> writes {};
	for (size_t i = 0; i < k_sections.size(); ++i) {
		infos[i] = vk::DescriptorBufferInfo(storage->buffer(k_sections[i]), 0, storage->size(k_sections[i]));
		// Section order matches voxel_dda.slang binding order
		writes[i] = vk::WriteDescriptorSet(
		    *m_scene_sets[frame_index], static_cast<uint32_t>(i), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &infos[i]
		);
	}

	// Safe since the fence of this slot was waited on
	m_core->getDevice().updateDescriptorSets(writes, {});
	m_bound_storage[frame_index] = storage;
}

void VoxelPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;

	if (frame_index >= m_scene_sets.size() || frame_index >= m_instance_buffers.size()) {
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->voxel_instances.empty() || !frame->voxel_storage || !frame->voxel_storage->isReady()) {
		return;
	}

	if (m_bound_storage[frame_index] != frame->voxel_storage) {
		bindStorage(frame_index, frame->voxel_storage);
	}

	const glm::mat4 voxel_scale = glm::scale(glm::mat4(1.0f), glm::vec3(toast::voxel::k_voxel_size));
	const glm::mat4 inverse_voxel_scale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / toast::voxel::k_voxel_size));

	auto& allocation = m_instance_buffers[frame_index].getAllocation();
	auto* instances = static_cast<InstanceGpu*>(allocation.getInfo().pMappedData);
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
		instances[index] = InstanceGpu {
		  .voxel_to_world = proxy.model * voxel_scale,
		  .world_to_voxel = inverse_voxel_scale * proxy.inverse_model,
		  .record_index = proxy.record_index,
		};
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
	allocation.flush(0, sizeof(InstanceGpu) * m_draws.size());

	const vk::PipelineLayout layout = *m_shader_layout.getPipelineLayout();
	const VulkanPipeline* bound = nullptr;

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
				    k_camera_set,
				    std::array<vk::DescriptorSet, 2> {*m_camera_sets[frame_index], *m_scene_sets[frame_index]},
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
