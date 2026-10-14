#include "voxel_pass.hpp"

#include "../depth_convention.hpp"
#include "../descriptor_writer.hpp"
#include "../gpu_sync.hpp"
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
#include <toast/voxel/gpu_layout.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

static_assert(sizeof(glm::mat4) == 64);

constexpr uint32_t k_scene_set = 0;
constexpr uint32_t k_storage_set = 1;
constexpr uint32_t k_instance_binding = 6;
constexpr uint32_t k_cells_binding = 7;
constexpr uint32_t k_cell_args_binding = 8;
constexpr uint32_t k_quads_binding = 9;
constexpr uint32_t k_hiz_binding = 10;
constexpr uint32_t k_hiz_view_binding = 11;
constexpr uint32_t k_vertices_per_quad = 6;

/// Readback words after the quad vertex count
constexpr uint32_t k_readback_words = 3;
/// Below this share of listed cells hidden the late passes cost more than they save
constexpr float k_occlusion_min_hidden = 0.05f;
constexpr uint32_t k_occlusion_low_frames = 16;
constexpr uint32_t k_occlusion_idle_frames = 120;

constexpr uint32_t k_cell_phase_count = 0;
constexpr uint32_t k_cell_phase_allocate = 1;
constexpr uint32_t k_cell_phase_write = 2;
constexpr uint32_t k_cell_phase_late = 3;
constexpr uint32_t k_cell_group_size = 64;

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
	static_assert(sizeof(CellCullPush) == 112, "CellCullPush is mirrored by voxel_cells.slang");

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

	// Quads arrive in no particular order so a depth only pass first leaves one shaded fragment per pixel
	config.cull_mode = vk::CullModeFlagBits::eBack;
	config.vertex_entry = "quadVertexMain";
	config.fragment_entry = "quadFragmentMain";
	config.depth_write = false;
	config.depth_compare = depth::k_closer_or_equal;
	config.debug_name = "VoxelPass Quads";
	m_quad_pipeline.rebuild(core, config);

	config.depth_only = true;
	config.depth_write = true;
	config.depth_compare = depth::k_closer;
	config.debug_name = "VoxelPass QuadDepth";
	m_quad_depth_pipeline.rebuild(core, config);

	// Shares the voxel.slang layout so the scene and storage sets bind unchanged
	if (core.isMeshShaderSupported()) {
		// Extension entry points are not exported by the loader library so they come from the device dispatcher
		m_draw_mesh_tasks_indirect = core.getDevice().getDispatcher()->vkCmdDrawMeshTasksIndirectEXT;
		const auto mesh_uid = assets::resolveURI("core://shaders/voxel_mesh.slang");
		const auto task_uid = assets::resolveURI("core://shaders/voxel_mesh_task.slang");
		const auto mesh_shader = mesh_uid.has_value() ? ShaderCache::get().acquire(*mesh_uid) : nullptr;
		const auto task_shader = task_uid.has_value() ? ShaderCache::get().acquire(*task_uid) : nullptr;
		if (mesh_shader && task_shader) {
			config.shader_spirv = mesh_shader->spirv;
			config.task_spirv = task_shader->spirv;
			config.task_entry = "quadTaskMain";
			config.mesh_entry = "quadMeshMain";
			config.fragment_entry = "meshQuadFragmentMain";
			// One pass with depth writes since a depth prepass would run the task and mesh shaders twice
			config.depth_only = false;
			config.depth_write = true;
			config.depth_compare = depth::k_closer;
			config.debug_name = "VoxelPass MeshQuads";
			m_mesh_quad_pipeline.rebuild(core, config);
		} else {
			TOAST_WARN(
			    "Render",
			    "VoxelPass shaders core://shaders/voxel_mesh.slang and voxel_mesh_task.slang unavailable; near voxels mesh in compute"
			);
		}
	}

	createInstanceBuffers(core);
	createDescriptors(core, shader->reflection);
	createCells(core);
	if (!m_cell_sets.empty()) {
		m_hiz.create(core, VulkanRenderer::instance->getDepthView(), extent);
		writeHizDescriptors();
	}
	createLighting(core, scene_format, extent);
	createNormalBlur(core, extent);
	TOAST_INFO(
	    "Render",
	    "VoxelPass ready: DDA into a G-buffer, up to {} volumes a frame, cell boxes {}",
	    k_max_instances,
	    m_cell_pipeline.isReady() ? "available" : "unavailable"
	);
}

void VoxelPass::createCells(const VulkanCore& core) {
	ZoneScoped;
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	m_cell_buffers.clear();
	m_cell_buffers.resize(m_storage_sets.size());
	m_cell_sets.clear();

	const auto uid = assets::resolveURI("core://shaders/voxel_cells.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (shader) {
		m_cell_layout.rebuild(core, shader->reflection, "VoxelCells");
		const auto& layouts = m_cell_layout.getDescriptorSetLayouts();
		if (layouts.size() > k_storage_set) {
			VulkanPipeline::Config config;
			config.pipeline_type = VulkanPipeline::PipelineType::compute;
			config.debug_name = "VoxelCells";
			config.shader_spirv = shader->spirv;
			config.pipeline_layout = *m_cell_layout.getPipelineLayout();
			m_cell_pipeline.rebuild(core, config);

			config.debug_name = "VoxelMesh";
			config.compute_entry = "meshMain";
			m_mesh_pipeline.rebuild(core, config);

			const vk::DescriptorSetLayout set_layout = *layouts[k_storage_set];
			for (uint32_t i = 0; i < m_storage_sets.size(); ++i) {
				auto allocated = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
				m_cell_sets.push_back(std::move(allocated[0]));
				setDebugName(core, *m_cell_sets.back(), std::format("VoxelCells StorageSet[{}]", i));

				DescriptorWriter writer;
				writer.buffer(*m_cell_sets.back(), k_instance_binding, vk::DescriptorType::eStorageBuffer, *m_instance_buffers[i]);
				writer.flush(core.getDevice());
			}
		}
	}
	if (!m_cell_pipeline.isReady()) {
		TOAST_WARN(
		    "Render", "VoxelPass cell boxes unavailable since core://shaders/voxel_cells.slang did not build, drawing whole volumes"
		);
		m_cell_sets.clear();
	}

	// The G-buffer set declares the cell bindings either way so they always hold a buffer
	for (uint32_t i = 0; i < m_cell_buffers.size(); ++i) {
		ensureCellCapacity(i, k_min_cell_capacity);
		ensureQuadCapacity(i, k_min_quad_capacity);

		vk::BufferCreateInfo view_ci {};
		view_ci.size = sizeof(HizView);
		view_ci.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		vma::AllocationCreateInfo view_alloc {};
		view_alloc.usage = vma::MemoryUsage::eAutoPreferHost;
		view_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
		m_cell_buffers[i].hiz_view.emplace(core.getAllocator().createBuffer(view_ci, view_alloc));
		setDebugName(core, **m_cell_buffers[i].hiz_view, std::format("VoxelPass HizView[{}]", i));
	}
}

void VoxelPass::writeHizDescriptors() {
	if (!m_hiz.view()) {
		return;
	}
	DescriptorWriter writer;
	for (uint32_t i = 0; i < m_cell_sets.size(); ++i) {
		writer.image(
		    *m_cell_sets[i],
		    k_hiz_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    m_hiz.sampler(),
		    m_hiz.view(),
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.buffer(*m_cell_sets[i], k_hiz_view_binding, vk::DescriptorType::eUniformBuffer, **m_cell_buffers[i].hiz_view);
	}
	writer.flush(m_core->getDevice());
}

void VoxelPass::ensureQuadCapacity(uint32_t frame_index, uint32_t quads) {
	CellBuffers& buffers = m_cell_buffers[frame_index];
	if (buffers.quads.has_value() && buffers.quad_capacity >= quads) {
		return;
	}
	ZoneScoped;

	const uint32_t capacity = std::max({quads + (quads / 4), k_min_quad_capacity, buffers.quad_capacity});

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	// Safe to replace since the fence of this slot was waited on
	vk::BufferCreateInfo quads_ci {};
	quads_ci.size = vk::DeviceSize {capacity} * sizeof(glm::uvec2);
	quads_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
	buffers.quads.emplace(m_core->getAllocator().createBuffer(quads_ci, alloc_ci));
	setDebugName(*m_core, **buffers.quads, std::format("VoxelPass Quads[{}]", frame_index));
	buffers.quad_capacity = capacity;

	if (!buffers.readback.has_value()) {
		vk::BufferCreateInfo readback_ci {};
		readback_ci.size = k_readback_words * sizeof(uint32_t);
		readback_ci.usage = vk::BufferUsageFlagBits::eTransferDst;

		vma::AllocationCreateInfo readback_alloc {};
		readback_alloc.usage = vma::MemoryUsage::eAuto;
		readback_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom;
		buffers.readback.emplace(m_core->getAllocator().createBuffer(readback_ci, readback_alloc));
		setDebugName(*m_core, **buffers.readback, std::format("VoxelPass QuadReadback[{}]", frame_index));
		if (auto* mapped = static_cast<uint32_t*>(buffers.readback->getAllocation().getInfo().pMappedData); mapped != nullptr) {
			std::fill_n(mapped, k_readback_words, 0u);
			buffers.readback->getAllocation().flush(0, k_readback_words * sizeof(uint32_t));
		}
	}

	writeCellDescriptors(frame_index);
}

void VoxelPass::ensureCellCapacity(uint32_t frame_index, uint32_t cells) {
	CellBuffers& buffers = m_cell_buffers[frame_index];
	if (buffers.cells.has_value() && buffers.capacity >= cells) {
		return;
	}
	ZoneScoped;

	// A quarter of headroom so a growing scene does not reallocate every frame
	const uint32_t capacity = std::max({cells + (cells / 4), k_min_cell_capacity, buffers.capacity});

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	// Safe to replace since the fence of this slot was waited on
	vk::BufferCreateInfo cells_ci {};
	cells_ci.size = vk::DeviceSize {capacity} * sizeof(glm::uvec2);
	cells_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
	buffers.cells.emplace(m_core->getAllocator().createBuffer(cells_ci, alloc_ci));
	setDebugName(*m_core, **buffers.cells, std::format("VoxelPass Cells[{}]", frame_index));
	buffers.capacity = capacity;

	if (!buffers.args.has_value()) {
		vk::BufferCreateInfo args_ci {};
		args_ci.size = vk::DeviceSize {k_cell_args_words} * sizeof(uint32_t);
		args_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
		                vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
		buffers.args.emplace(m_core->getAllocator().createBuffer(args_ci, alloc_ci));
		setDebugName(*m_core, **buffers.args, std::format("VoxelPass CellArgs[{}]", frame_index));
	}

	writeCellDescriptors(frame_index);
}

void VoxelPass::writeCellDescriptors(uint32_t frame_index) {
	const CellBuffers& buffers = m_cell_buffers[frame_index];
	// The cell and quad buffers come up one after the other at creation
	if (!buffers.cells.has_value() || !buffers.quads.has_value()) {
		return;
	}

	DescriptorWriter writer;
	const auto write = [&](vk::DescriptorSet set) {
		writer.buffer(set, k_cells_binding, vk::DescriptorType::eStorageBuffer, **buffers.cells);
		writer.buffer(set, k_cell_args_binding, vk::DescriptorType::eStorageBuffer, **buffers.args);
		writer.buffer(set, k_quads_binding, vk::DescriptorType::eStorageBuffer, **buffers.quads);
	};
	write(*m_storage_sets[frame_index]);
	if (frame_index < m_cell_sets.size()) {
		write(*m_cell_sets[frame_index]);
	}
	writer.flush(m_core->getDevice());
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
	if (frame_index < m_cell_sets.size()) {
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_cell_sets[frame_index], *storage);
	}
	m_bound_storage[frame_index] = storage;
}

auto VoxelPass::prepare(uint32_t frame_index) -> bool {
	ZoneScoped;
	m_draws.clear();
	m_cells_active = false;
	m_occlusion_active = false;
	++m_frame_count;
	m_total_cells = 0;
	m_near_voxels = 0.0f;
	m_mesh_shading = false;

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

	bool cells_fit = true;

	for (const auto& proxy : frame->voxel_instances) {
		if (!proxy.visible) {
			continue;
		}
		if (m_draws.size() >= k_max_instances) {
			const auto now = std::chrono::steady_clock::now();
			if (now - m_last_limit_warning > std::chrono::seconds(5)) {
				m_last_limit_warning = now;
				const auto visible = std::ranges::count_if(frame->voxel_instances, [](const auto& p) { return p.visible; });
				TOAST_WARN("Render", "VoxelPass: {} visible volumes, only the first {} are drawn", visible, k_max_instances);
			}
			break;
		}

		const glm::uvec3 coarse = voxel::gpu::coarseDims(proxy.brick_dims);
		cells_fit = cells_fit && glm::all(glm::lessThanEqual(coarse, glm::uvec3(k_max_coarse_dim)));

		const auto index = static_cast<uint32_t>(m_draws.size());
		instances[index] =
		    makeVoxelInstance(proxy.model, proxy.inverse_model, proxy.previous_model, proxy.has_previous, proxy.record_index);
		instances[index].pad0 = proxy.highlight_record;
		instances[index].cell_base = m_total_cells;
		if (proxy.mirrored) {
			instances[index].flags |= k_voxel_instance_mirrored;
		}
		m_total_cells += coarse.x * coarse.y * coarse.z;

		// voxel.slang flips the winding of mirrored instances so the cull mode only follows the camera
		m_draws.push_back(
		    Draw {
		      .instance = index,
		      .cull = proxy.camera_inside ? Cull::front : Cull::back,
		      .depth = proxy.camera_inside ? Depth::exact : Depth::conservative,
		    }
		);
	}
	if (m_draws.empty()) {
		return false;
	}
	allocation.flush(0, sizeof(VoxelInstanceGpu) * m_draws.size());

	// Before any scope of this frame binds the set since updating a bound set invalidates the command buffer
	m_scene_sets.updateTlas(frame_index);

	// The cell set binds the pyramid view so it must exist even with occlusion off
	if (VulkanRenderer::instance->voxelCellBoxes() && m_cell_pipeline.isReady() && m_hiz.view()) {
		if (cells_fit) {
			// Grows from what this slot meshed last time so a spike costs at most one frame of missing quads
			CellBuffers& buffers = m_cell_buffers[frame_index];
			const auto& readback = buffers.readback->getAllocation();
			readback.invalidate(0, k_readback_words * sizeof(uint32_t));
			if (const auto* words = static_cast<const uint32_t*>(readback.getInfo().pMappedData); words != nullptr) {
				ensureQuadCapacity(frame_index, words[0] / k_vertices_per_quad);
				if (buffers.measured) {
					updateOcclusionMode(words[1], words[2]);
				}
			}

			const bool wanted = VulkanRenderer::instance->voxelOcclusion() && m_hiz.isReady();
			if (!wanted) {
				m_occlusion_mode = OcclusionMode::engaged;
				m_occlusion_low_frames = 0;
			} else if (m_occlusion_mode == OcclusionMode::idle && m_occlusion_idle_frames-- == 0) {
				m_occlusion_mode = OcclusionMode::engaged;
				m_occlusion_low_frames = 0;
			}

			// Late survivors go after every other list and are at most the retest cells
			m_occlusion_active = wanted && m_occlusion_mode == OcclusionMode::engaged;
			ensureCellCapacity(frame_index, m_occlusion_active ? 2 * m_total_cells : m_total_cells);
			m_cells_active = true;

			// A voxel covers at least this many pixels closer than near_voxels voxels whatever the volume scale
			const float raster_pixels = VulkanRenderer::instance->voxelRasterPixels();
			const float height = static_cast<float>(VulkanRenderer::instance->getOutputTarget().getExtent().height);
			m_near_voxels =
			    raster_pixels > 0.0f && m_mesh_pipeline.isReady() && m_quad_pipeline.isReady() && m_quad_depth_pipeline.isReady()
			        ? std::abs(frame->frame_data.projection[1][1]) * height / (2.0f * raster_pixels)
			        : 0.0f;
			m_mesh_shading = m_near_voxels > 0.0f && VulkanRenderer::instance->voxelMeshShaders() &&
			                 m_draw_mesh_tasks_indirect != nullptr && m_mesh_quad_pipeline.isReady();
		} else if (const auto now = std::chrono::steady_clock::now(); now - m_last_limit_warning > std::chrono::seconds(5)) {
			m_last_limit_warning = now;
			TOAST_WARN(
			    "Render", "VoxelPass: a volume spans more than {} coarse cells on one axis, drawing whole volumes", k_max_coarse_dim
			);
		}
	}
	return true;
}

void VoxelPass::recordCellCull(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D scene_extent) {
	ZoneScoped;
	if (!m_cells_active) {
		return;
	}
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		m_cells_active = false;
		return;
	}

	const CellBuffers& buffers = m_cell_buffers[frame_index];
	const vk::Buffer args = **buffers.args;
	const vk::Buffer cells = **buffers.cells;

	// The count phase accumulates into the cursors
	cmd.fillBuffer(
	    args, vk::DeviceSize {k_cell_cursors_word} * sizeof(uint32_t), vk::DeviceSize {k_cell_list_count} * sizeof(uint32_t), 0
	);

	// A pyramid from another extent such as a capture frame says nothing about this one
	const bool early_occlusion = m_occlusion_active && m_pyramid_frame + 1 == m_frame_count && m_hiz.builtLevels() > 0 &&
	                             m_hiz.builtExtent() == scene_extent;
	m_cell_buffers[frame_index].measured = early_occlusion;
	if (early_occlusion) {
		const auto& view = buffers.hiz_view->getAllocation();
		if (auto* mapped = static_cast<HizView*>(view.getInfo().pMappedData); mapped != nullptr) {
			mapped->view_projection = m_hiz.builtViewProjection();
			view.flush(0, sizeof(HizView));
		}
	}

	sync::BarrierBatch barriers;
	barriers.buffer(args, sync::Usage::transfer_dst, sync::Usage::compute_storage_write);
	m_hiz.beforeRead(barriers);
	barriers.flush(cmd);

	const vk::PipelineLayout layout = *m_cell_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cell_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute, layout, k_storage_set, std::array<vk::DescriptorSet, 1> {*m_cell_sets[frame_index]}, {}
	);

	CellCullPush push {
	  .view_projection = frame->frame_data.view_projection,
	  .camera_near = glm::vec4(frame->frame_data.camera_position, frame->near_plane),
	  .total_cells = m_total_cells,
	  .instance_count = static_cast<uint32_t>(m_draws.size()),
	  .quad_capacity = buffers.quad_capacity,
	  .near_voxels = m_near_voxels,
	  .hiz_levels = early_occlusion ? m_hiz.builtLevels() : 0,
	  .hiz_width = m_hiz.builtExtent().width,
	  .hiz_height = m_hiz.builtExtent().height,
	};
	const uint32_t groups = (m_total_cells + k_cell_group_size - 1) / k_cell_group_size;

	for (const uint32_t phase : {k_cell_phase_count, k_cell_phase_allocate, k_cell_phase_write}) {
		push.phase = phase;
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(CellCullPush), &push);
		cmd.dispatch(phase == k_cell_phase_allocate ? 1 : groups, 1, 1);
		if (phase != k_cell_phase_write) {
			barriers.buffer(args, sync::Usage::compute_storage_write, sync::Usage::compute_storage_write);
			barriers.flush(cmd);
		}
	}
	m_cull_push = push;

	if (m_near_voxels > 0.0f && !m_mesh_shading) {
		barriers.buffer(args, sync::Usage::compute_storage_write, sync::Usage::indirect_read);
		barriers.buffer(args, sync::Usage::compute_storage_write, sync::Usage::compute_storage_write);
		barriers.buffer(cells, sync::Usage::compute_storage_write, sync::Usage::compute_storage_read);
		barriers.flush(cmd);

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_mesh_pipeline.getPipeline());
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(CellCullPush), &push);
		cmd.dispatchIndirect(args, vk::DeviceSize {k_near_dispatch_word} * sizeof(uint32_t));
	}
	if (!m_occlusion_active) {
		recordReadback(cmd, frame_index, barriers);
	}
	afterCullWrites(cmd, frame_index, barriers);
}

void VoxelPass::afterCullWrites(vk::CommandBuffer cmd, uint32_t frame_index, sync::BarrierBatch& barriers) {
	const CellBuffers& buffers = m_cell_buffers[frame_index];
	barriers.buffer(**buffers.args, sync::Usage::compute_storage_write, sync::Usage::indirect_read);
	barriers.buffer(**buffers.args, sync::Usage::compute_storage_write, sync::Usage::vertex_storage_read);
	barriers.buffer(**buffers.cells, sync::Usage::compute_storage_write, sync::Usage::vertex_storage_read);
	barriers.buffer(**buffers.quads, sync::Usage::compute_storage_write, sync::Usage::vertex_storage_read);
	if (m_mesh_shading) {
		barriers.buffer(**buffers.args, sync::Usage::compute_storage_write, sync::Usage::mesh_storage_read);
		barriers.buffer(**buffers.cells, sync::Usage::compute_storage_write, sync::Usage::mesh_storage_read);
	}
	barriers.flush(cmd);
}

void VoxelPass::recordNearQuads(vk::CommandBuffer cmd, uint32_t frame_index, bool late, bool depth_only) {
	const vk::PipelineLayout layout = *m_shader_layout.getPipelineLayout();
	const CellBuffers& buffers = m_cell_buffers[frame_index];
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_scene_set,
	    std::array<vk::DescriptorSet, 2> {m_scene_sets.get(frame_index), *m_storage_sets[frame_index]},
	    {}
	);

	if (m_mesh_shading) {
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_mesh_quad_pipeline.getPipeline());
		// voxel_mesh.slang reads the bucket as which near list to draw
		const PushConstants push {.bucket = late ? 1u : 0u, .mode = k_draw_cells};
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &push);
		const uint32_t word = late ? k_late_task_word : k_near_task_word;
		m_draw_mesh_tasks_indirect(
		    static_cast<VkCommandBuffer>(cmd),
		    static_cast<VkBuffer>(**buffers.args),
		    vk::DeviceSize {word} * sizeof(uint32_t),
		    1,
		    static_cast<uint32_t>(sizeof(vk::DrawMeshTasksIndirectCommandEXT))
		);
		return;
	}

	const VulkanPipeline& pipeline = depth_only ? m_quad_depth_pipeline : m_quad_pipeline;
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
	const PushConstants push {.mode = k_draw_cells, .quad_capacity = buffers.quad_capacity};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &push);
	const uint32_t word = late ? k_late_quad_draw_word : k_quad_draw_word;
	cmd.drawIndirect(
	    **buffers.args, vk::DeviceSize {word} * sizeof(uint32_t), 1, static_cast<uint32_t>(sizeof(vk::DrawIndirectCommand))
	);
}

void VoxelPass::recordReadback(vk::CommandBuffer cmd, uint32_t frame_index, sync::BarrierBatch& barriers) {
	const CellBuffers& buffers = m_cell_buffers[frame_index];
	barriers.buffer(**buffers.args, sync::Usage::compute_storage_write, sync::Usage::transfer_src);
	barriers.flush(cmd);
	constexpr auto k_word = static_cast<vk::DeviceSize>(sizeof(uint32_t));
	const std::array regions {
	  vk::BufferCopy(k_quad_draw_word * k_word, 0, k_word),
	  vk::BufferCopy(k_occlusion_stats_word * k_word, k_word, 2 * k_word),
	};
	cmd.copyBuffer(**buffers.args, **buffers.readback, regions);
	barriers.buffer(**buffers.readback, sync::Usage::transfer_dst, sync::Usage::host_read);
}

void VoxelPass::updateOcclusionMode(uint32_t retest_cells, uint32_t listed_cells) {
	if (m_occlusion_mode != OcclusionMode::engaged) {
		return;
	}
	const float hidden = static_cast<float>(retest_cells) / static_cast<float>(std::max(listed_cells, 1u));
	m_occlusion_low_frames = hidden < k_occlusion_min_hidden ? m_occlusion_low_frames + 1 : 0;
	if (m_occlusion_low_frames >= k_occlusion_low_frames) {
		m_occlusion_mode = OcclusionMode::idle;
		m_occlusion_idle_frames = k_occlusion_idle_frames;
	}
}

void VoxelPass::recordCellDraws(vk::CommandBuffer cmd, uint32_t frame_index) {
	const vk::PipelineLayout layout = *m_shader_layout.getPipelineLayout();
	const vk::Buffer args = **m_cell_buffers[frame_index].args;
	constexpr auto k_command_stride = static_cast<uint32_t>(sizeof(vk::DrawIndirectCommand));

	bool sets_bound = false;
	const auto bind = [&](const VulkanPipeline& pipeline) {
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
		if (!sets_bound) {
			cmd.bindDescriptorSets(
			    vk::PipelineBindPoint::eGraphics,
			    layout,
			    k_scene_set,
			    std::array<vk::DescriptorSet, 2> {m_scene_sets.get(frame_index), *m_storage_sets[frame_index]},
			    {}
			);
			sets_bound = true;
		}
	};
	const auto draw_bucket = [&](uint32_t bucket) {
		const PushConstants push {.bucket = bucket, .mode = k_draw_cells};
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &push);
		cmd.drawIndirect(args, vk::DeviceSize {bucket} * k_command_stride, 1, k_command_stride);
	};

	// Near quads first so their depth rejects the far boxes behind them before they march
	if (m_near_voxels > 0.0f) {
		recordNearQuads(cmd, frame_index, false, false);
		sets_bound = true;
	}

	// Cells holding the camera are nearest so they go first and seed depth for the rest
	const VulkanPipeline& inside = m_pipelines[static_cast<size_t>(Cull::front)][static_cast<size_t>(Depth::exact)];
	if (inside.isReady()) {
		bind(inside);
		draw_bucket(0);
	}

	// Front to back by bucket so early depth testing rejects hidden cells before they march
	const VulkanPipeline& outside = m_pipelines[static_cast<size_t>(Cull::back)][static_cast<size_t>(Depth::conservative)];
	if (outside.isReady()) {
		bind(outside);
		for (uint32_t bucket = 1; bucket < k_cell_bucket_count; ++bucket) {
			draw_bucket(bucket);
		}
	}
}

void VoxelPass::recordOcclusion(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D scene_extent) {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (!m_occlusion_active || frame == nullptr) {
		return;
	}

	m_hiz.record(cmd, scene_extent, frame->frame_data.view_projection);
	m_pyramid_frame = m_frame_count;

	const CellBuffers& buffers = m_cell_buffers[frame_index];
	const vk::Buffer args = **buffers.args;
	const vk::Buffer cells = **buffers.cells;
	const vk::Buffer quads = **buffers.quads;

	// The early draws read all three before the late phases append to them
	sync::BarrierBatch barriers;
	barriers.buffer(args, sync::Usage::vertex_storage_read, sync::Usage::compute_storage_write);
	barriers.buffer(args, sync::Usage::indirect_read, sync::Usage::compute_storage_write);
	barriers.buffer(cells, sync::Usage::vertex_storage_read, sync::Usage::compute_storage_write);
	barriers.buffer(quads, sync::Usage::vertex_storage_read, sync::Usage::compute_storage_write);
	if (m_mesh_shading) {
		barriers.buffer(args, sync::Usage::mesh_storage_read, sync::Usage::compute_storage_write);
		barriers.buffer(cells, sync::Usage::mesh_storage_read, sync::Usage::compute_storage_write);
	}
	barriers.flush(cmd);

	CellCullPush push = m_cull_push;
	push.phase = k_cell_phase_late;
	push.hiz_levels = m_hiz.builtLevels();
	push.hiz_width = m_hiz.builtExtent().width;
	push.hiz_height = m_hiz.builtExtent().height;

	const vk::PipelineLayout layout = *m_cell_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cell_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute, layout, k_storage_set, std::array<vk::DescriptorSet, 1> {*m_cell_sets[frame_index]}, {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(CellCullPush), &push);
	cmd.dispatchIndirect(args, vk::DeviceSize {k_retest_dispatch_word} * sizeof(uint32_t));

	if (m_near_voxels > 0.0f && !m_mesh_shading) {
		barriers.buffer(args, sync::Usage::compute_storage_write, sync::Usage::indirect_read);
		barriers.buffer(args, sync::Usage::compute_storage_write, sync::Usage::compute_storage_write);
		barriers.buffer(cells, sync::Usage::compute_storage_write, sync::Usage::compute_storage_read);
		barriers.flush(cmd);

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_mesh_pipeline.getPipeline());
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(CellCullPush), &push);
		cmd.dispatchIndirect(args, vk::DeviceSize {k_late_mesh_dispatch_word} * sizeof(uint32_t));
	}
	recordReadback(cmd, frame_index, barriers);
	afterCullWrites(cmd, frame_index, barriers);
}

void VoxelPass::recordQuadDepth(vk::CommandBuffer cmd, uint32_t frame_index, bool late) {
	ZoneScoped;
	if (nearQuadDepthPass()) {
		recordNearQuads(cmd, frame_index, late, true);
	}
}

void VoxelPass::recordLate(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	const VulkanPipeline& outside = m_pipelines[static_cast<size_t>(Cull::back)][static_cast<size_t>(Depth::conservative)];
	if (!m_occlusion_active || !outside.isReady()) {
		return;
	}

	const vk::PipelineLayout layout = *m_shader_layout.getPipelineLayout();
	const CellBuffers& buffers = m_cell_buffers[frame_index];
	constexpr auto k_command_stride = static_cast<uint32_t>(sizeof(vk::DrawIndirectCommand));
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_scene_set,
	    std::array<vk::DescriptorSet, 2> {m_scene_sets.get(frame_index), *m_storage_sets[frame_index]},
	    {}
	);

	// Late quads first so their depth rejects the late boxes behind them
	if (m_near_voxels > 0.0f) {
		recordNearQuads(cmd, frame_index, true, false);
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, outside.getPipeline());
	const PushConstants push {.bucket = k_late_list, .mode = k_draw_cells};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &push);
	cmd.drawIndirect(**buffers.args, vk::DeviceSize {k_late_list} * k_command_stride, 1, k_command_stride);
}

void VoxelPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;

	if (m_draws.empty()) {
		return;
	}

	if (m_cells_active) {
		recordCellDraws(cmd, frame_index);
		return;
	}

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

void VoxelPass::onTargetsRecreated() {
	std::ranges::fill(m_bound_gbuffer, std::array<vk::ImageView, 5> {});
	std::ranges::fill(m_bound_blur, std::array<vk::ImageView, 3> {});

	if (!m_cell_sets.empty()) {
		const auto* renderer = VulkanRenderer::instance;
		m_hiz.create(*m_core, renderer->getDepthView(), renderer->getOutputTarget().getExtent());
		writeHizDescriptors();
	}
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
