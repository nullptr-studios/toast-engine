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
#include <bit>
#include <cmath>
#include <format>
#include <numeric>
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
constexpr uint32_t k_cell_class_binding = 12;
constexpr uint32_t k_vertices_per_quad = 6;

/// Readback words after the quad vertex count
constexpr uint32_t k_readback_words = 3;
/// Measured frames below the renderer minimum hidden share before occlusion idles
constexpr uint32_t k_occlusion_low_frames = 16;

constexpr uint32_t k_cell_phase_count = 0;
constexpr uint32_t k_cell_phase_allocate = 1;
constexpr uint32_t k_cell_phase_write = 2;
constexpr uint32_t k_cell_phase_late = 3;
constexpr uint32_t k_cell_group_size = 64;

/// Albedo face normal material and depth at bindings 0 to 3 of voxel_lighting.slang
constexpr uint32_t k_gbuffer_set = 2;

/// voxel_lighting.slang lighting.z
constexpr uint32_t k_marched_none = 0;
constexpr uint32_t k_marched_into_terms = 1;
constexpr uint32_t k_marched_into_colour = 2;

/// voxel_denoise.slang spiral radius in pixels
constexpr float k_denoise_radius = 6.0f;

/// voxel_lighting.slang gInstanceBounds
constexpr uint32_t k_instance_bounds_binding = 7;

/// voxel_lighting.slang gAmbientGrid
constexpr uint32_t k_ambient_grid_binding = 8;
/// Mirrored by voxel_lighting.slang
constexpr uint32_t k_ambient_grid_buckets = 4096;
constexpr uint32_t k_ambient_grid_header = 4;
constexpr uint32_t k_ambient_grid_capacity = 65536;
/// More cells than this goes to the global list
constexpr uint64_t k_ambient_grid_max_cells = 125;

auto ambientGridHash(const glm::ivec3& cell) -> uint32_t {
	const glm::uvec3 u(cell);
	return ((u.x * 73856093u) ^ (u.y * 19349663u) ^ (u.z * 83492791u)) & (k_ambient_grid_buckets - 1u);
}

constexpr uint32_t k_depth_binding = 3;

/// voxel.slang gFreshRegions
constexpr uint32_t k_fresh_regions_binding = 13;
/// Regions a frame marks reactive and the rest wait for no later frame
constexpr uint32_t k_max_fresh_regions = 255;

/// voxel_lighting.slang reuse.x bits
constexpr uint32_t k_reuse_history = 1;
constexpr uint32_t k_reuse_spatial = 2;

/// ReSTIR reuse of last frame's reservoir at the reprojected pixel and two around it
constexpr uint32_t k_reused_reservoirs = 3;
/// Temporal M cap as a multiple of the fresh candidates
constexpr uint32_t k_reservoir_m_cap = 20;

/// voxel_glass.slang gGlassBricks
constexpr uint32_t k_glass_bricks_binding = 7;

/// Turns the rounding spiral each frame for TAA to average
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
	createGlass(core, scene_format, depth_format, extent);
	createDenoise(core, scene_format, extent);
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
	buffers.classes.emplace(m_core->getAllocator().createBuffer(cells_ci, alloc_ci));
	setDebugName(*m_core, **buffers.classes, std::format("VoxelPass CellClasses[{}]", frame_index));
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
		writer.buffer(*m_cell_sets[frame_index], k_cell_class_binding, vk::DescriptorType::eStorageBuffer, **buffers.classes);
	}
	writer.flush(m_core->getDevice());
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
	config.extra_color_formats = {
	  k_scene_indirect_format, k_scene_normal_format, k_voxel_noisy_format, k_voxel_noisy_format, k_voxel_reservoir_format
	};
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

	config.debug_name = "VoxelSpatialReuse";
	config.fragment_entry = "fragmentSpatial";
	config.color_format = k_voxel_noisy_format;
	config.extra_color_formats = {k_voxel_noisy_format, k_voxel_reservoir_format};
	config.blend_preset = VulkanPipeline::BlendPreset::additive;
	config.extra_blend_mask = 1u;
	m_spatial_pipeline.rebuild(core, config);

	m_lighting_scene_sets.create(core, shader->reflection, *layouts[k_scene_set], "VoxelLighting");

	const vk::DescriptorSetLayout storage_layout = *layouts[k_storage_set];
	m_lighting_storage_sets.clear();
	m_instance_bounds.clear();
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo bounds_ci {};
		bounds_ci.size = sizeof(glm::vec4) * k_max_instances;
		bounds_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo bounds_alloc {};
		bounds_alloc.usage = vma::MemoryUsage::eAutoPreferHost;
		bounds_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
		m_instance_bounds.push_back(core.getAllocator().createBuffer(bounds_ci, bounds_alloc));
		setDebugName(core, *m_instance_bounds.back(), std::format("VoxelLighting InstanceBounds[{}]", i));

		vk::BufferCreateInfo grid_ci {};
		grid_ci.size = sizeof(uint32_t) * (k_ambient_grid_header + (2 * k_ambient_grid_buckets) + k_ambient_grid_capacity);
		grid_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		m_ambient_grids.push_back(core.getAllocator().createBuffer(grid_ci, bounds_alloc));
		setDebugName(core, *m_ambient_grids.back(), std::format("VoxelLighting AmbientGrid[{}]", i));
		if (auto* words = static_cast<uint32_t*>(m_ambient_grids.back().getAllocation().getInfo().pMappedData)) {
			std::fill_n(words, k_ambient_grid_header + (2 * k_ambient_grid_buckets), 0u);
			m_ambient_grids.back().getAllocation().flush(0, vk::WholeSize);
		}

		auto allocated = core.getDevice().allocateDescriptorSets(
		    vk::DescriptorSetAllocateInfo(VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &storage_layout)
		);
		m_lighting_storage_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_lighting_storage_sets.back(), std::format("VoxelLighting StorageSet[{}]", i));

		DescriptorWriter writer;
		writer.buffer(
		    *m_lighting_storage_sets.back(), k_instance_binding, vk::DescriptorType::eStorageBuffer, *m_instance_buffers[i]
		);
		writer.buffer(
		    *m_lighting_storage_sets.back(), k_instance_bounds_binding, vk::DescriptorType::eStorageBuffer, *m_instance_bounds.back()
		);
		writer.buffer(
		    *m_lighting_storage_sets.back(), k_ambient_grid_binding, vk::DescriptorType::eStorageBuffer, *m_ambient_grids.back()
		);
		writer.flush(core.getDevice());
	}

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

void VoxelPass::createGlass(const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/voxel_glass.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_WARN("Render", "VoxelPass shader core://shaders/voxel_glass.slang unavailable; voxel glass will not draw");
		return;
	}

	m_glass_layout.rebuild(core, shader->reflection, "VoxelGlass");
	const auto& layouts = m_glass_layout.getDescriptorSetLayouts();
	if (layouts.size() <= k_storage_set) {
		TOAST_WARN("Render", "VoxelGlass shader layout declares {} descriptor sets; voxel glass will not draw", layouts.size());
		return;
	}

	const bool dual_source = core.isDualSourceBlendSupported();

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "VoxelGlass";
	config.color_format = scene_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.extra_color_formats = worldStageExtraColorFormats();
	config.write_extra_color = false;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_glass_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.fragment_entry = dual_source ? "fragmentMain" : "fragmentPremultiplied";
	// Back faces so a camera inside a brick still draws it
	config.cull_mode = vk::CullModeFlagBits::eFront;
	config.depth_test = true;
	config.depth_write = false;
	config.depth_compare = depth::k_closer;
	config.blend_preset = dual_source ? VulkanPipeline::BlendPreset::dual_source : VulkanPipeline::BlendPreset::premultiplied;
	m_glass_pipeline.rebuild(core, config);

	config.debug_name = "VoxelGlass Surface";
	config.fragment_entry = "fragmentSurface";
	config.blend_preset = VulkanPipeline::BlendPreset::none;
	config.write_color = false;
	config.write_extra_color = true;
	config.depth_write = true;
	m_glass_surface_pipeline.rebuild(core, config);

	if (!dual_source) {
		TOAST_WARN("Render", "dualSrcBlend unsupported so voxel glass fades the background without tinting it");
	}

	m_glass_scene_sets.create(core, shader->reflection, *layouts[k_scene_set], "VoxelGlass");

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout glass_layout = *layouts[k_storage_set];
	m_glass_sets.clear();
	m_glass_buffers.clear();
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(glm::uvec2) * k_max_glass_bricks;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
		m_glass_buffers.push_back(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(core, *m_glass_buffers.back(), std::format("VoxelGlass Bricks[{}]", i));

		auto allocated = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &glass_layout));
		m_glass_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_glass_sets.back(), std::format("VoxelGlass StorageSet[{}]", i));

		DescriptorWriter writer;
		writer.buffer(*m_glass_sets.back(), k_instance_binding, vk::DescriptorType::eStorageBuffer, *m_instance_buffers[i]);
		writer.buffer(*m_glass_sets.back(), k_glass_bricks_binding, vk::DescriptorType::eStorageBuffer, *m_glass_buffers.back());
		writer.flush(core.getDevice());
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

		vk::BufferCreateInfo regions_ci {};
		regions_ci.size = sizeof(glm::vec4) * (1 + (2 * k_max_fresh_regions));
		regions_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo regions_alloc {};
		regions_alloc.usage = vma::MemoryUsage::eAutoPreferHost;
		regions_alloc.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
		m_fresh_regions.push_back(core.getAllocator().createBuffer(regions_ci, regions_alloc));
		setDebugName(core, *m_fresh_regions.back(), std::format("VoxelPass FreshRegions[{}]", i));
		if (auto* mapped = static_cast<glm::vec4*>(m_fresh_regions.back().getAllocation().getInfo().pMappedData); mapped != nullptr) {
			mapped[0] = glm::vec4(0.0f);
			m_fresh_regions.back().getAllocation().flush(0, sizeof(glm::vec4));
		}

		DescriptorWriter regions_writer;
		regions_writer.buffer(
		    *m_storage_sets[i], k_fresh_regions_binding, vk::DescriptorType::eStorageBuffer, *m_fresh_regions.back()
		);
		regions_writer.flush(device);
	}
}

void VoxelPass::bindStorage(uint32_t frame_index, const std::shared_ptr<const VoxelGpuStorage>& storage) {
	ZoneScoped;
	// Safe since the fence of this slot was waited on
	writeVoxelStorageDescriptors(m_core->getDevice(), *m_storage_sets[frame_index], *storage);
	if (frame_index < m_cell_sets.size()) {
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_cell_sets[frame_index], *storage);
	}
	if (frame_index < m_glass_sets.size()) {
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_glass_sets[frame_index], *storage);
	}
	if (frame_index < m_lighting_storage_sets.size()) {
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_lighting_storage_sets[frame_index], *storage);
	}
	m_bound_storage[frame_index] = storage;
}

void VoxelPass::writeAmbientGrid(uint32_t frame_index, std::span<const glm::vec4> bounds) {
	ZoneScoped;
	if (frame_index >= m_ambient_grids.size()) {
		return;
	}
	auto& grid_allocation = m_ambient_grids[frame_index].getAllocation();
	auto* words = static_cast<uint32_t*>(grid_allocation.getInfo().pMappedData);
	if (words == nullptr) {
		return;
	}

	// Centimetres like the push constant
	const float reach = static_cast<float>(std::lround(VulkanRenderer::instance->voxelAmbientReach() * 100.0f)) * 0.01f;
	const float cell_size = std::max(reach, 0.5f);

	m_grid_pairs.clear();
	m_grid_global.clear();
	for (uint32_t index = 0; index < bounds.size(); ++index) {
		const glm::vec3 center(bounds[index]);
		const float radius = bounds[index].w + reach;
		const glm::ivec3 low(glm::floor((center - radius) / cell_size));
		const glm::ivec3 high(glm::floor((center + radius) / cell_size));
		const glm::u64vec3 span(glm::max(high - low + 1, glm::ivec3(1)));
		if (span.x * span.y * span.z > k_ambient_grid_max_cells) {
			m_grid_global.push_back(index);
			continue;
		}
		for (int32_t z = low.z; z <= high.z; ++z) {
			for (int32_t y = low.y; y <= high.y; ++y) {
				for (int32_t x = low.x; x <= high.x; ++x) {
					m_grid_pairs.emplace_back(ambientGridHash({x, y, z}), index);
				}
			}
		}
	}
	std::ranges::sort(m_grid_pairs);
	const auto duplicates = std::ranges::unique(m_grid_pairs);
	m_grid_pairs.erase(duplicates.begin(), duplicates.end());

	if (m_grid_pairs.size() + m_grid_global.size() > k_ambient_grid_capacity) {
		m_grid_pairs.clear();
		m_grid_global.resize(bounds.size());
		std::iota(m_grid_global.begin(), m_grid_global.end(), 0u);
	}

	uint32_t* buckets = words + k_ambient_grid_header;
	uint32_t* indices = buckets + (2 * k_ambient_grid_buckets);
	std::fill_n(buckets, 2 * k_ambient_grid_buckets, 0u);
	uint32_t cursor = 0;
	for (size_t i = 0; i < m_grid_pairs.size();) {
		const uint32_t bucket = m_grid_pairs[i].first;
		buckets[2 * bucket] = cursor;
		for (; i < m_grid_pairs.size() && m_grid_pairs[i].first == bucket; ++i) {
			indices[cursor++] = m_grid_pairs[i].second;
		}
		buckets[(2 * bucket) + 1] = cursor - buckets[2 * bucket];
	}
	const uint32_t global_offset = cursor;
	for (const uint32_t index : m_grid_global) {
		indices[cursor++] = index;
	}

	words[0] = std::bit_cast<uint32_t>(cell_size);
	words[1] = global_offset;
	words[2] = static_cast<uint32_t>(m_grid_global.size());
	words[3] = 0;
	grid_allocation.flush(0, sizeof(uint32_t) * (k_ambient_grid_header + (2 * k_ambient_grid_buckets) + cursor));
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
	m_glass_count = 0;
	m_shadow_instance_count = 0;
	m_marched_lights = false;
	m_ambient_rays = false;
	m_denoise_active = false;
	m_spatial_active = false;

	if (frame_index >= m_storage_sets.size() || frame_index >= m_instance_buffers.size() || !m_scene_sets.get(frame_index) ||
	    !m_lighting_pipeline.isReady()) {
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
	auto* bounds = frame_index < m_instance_bounds.size()
	                   ? static_cast<glm::vec4*>(m_instance_bounds[frame_index].getAllocation().getInfo().pMappedData)
	                   : nullptr;

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
		if (bounds != nullptr) {
			bounds[index] = glm::vec4(proxy.bounds_center, proxy.bounds_radius);
		}
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

	writeFreshRegions(frame_index, *frame->voxel_storage);

	m_marched_lights =
	    std::ranges::any_of(frame->lights, [](const VulkanRenderer::GpuLight& light) { return light.shape.w > 0.5f; });
	const uint32_t suns = std::min(frame->frame_data.directional_light_count_pad.x, VulkanRenderer::k_max_directional_lights);
	for (uint32_t i = 0; i < suns; ++i) {
		m_marched_lights = m_marched_lights || frame->frame_data.directional_lights[i].direction.w > 0.0f;
	}
	m_ambient_rays = VulkanRenderer::instance->voxelAmbientRays() && VulkanRenderer::instance->voxelAmbientReach() > 0.0f;

	// Debug views and probe captures skip the denoiser
	const bool capture = frame->probe_capture_index >= 0 || frame->irradiance_capture_index >= 0;
	m_denoise_active = (m_marched_lights || m_ambient_rays) && frame->render_mode == 0 && !capture &&
	                   m_denoise_temporal_pipeline.isReady() && m_denoise_composite_pipeline.isReady() &&
	                   frame_index * 2 + 1 < m_denoise_sets.size() && m_denoise_history[1][1].isReady();
	if (!m_denoise_active) {
		m_denoise_valid = false;
	}
	// Without the denoiser sampled lights shade in the lighting scope
	m_spatial_active =
	    m_denoise_active && m_spatial_pipeline.isReady() &&
	    std::ranges::any_of(frame->lights, [](const VulkanRenderer::GpuLight& light) { return light.shape.w > 1.5f; });

	// Off screen volumes follow the drawn ones for shadow rays only
	m_shadow_instance_count = static_cast<uint32_t>(m_draws.size());
	for (const auto& proxy : frame->voxel_instances) {
		if (proxy.visible || bounds == nullptr || m_shadow_instance_count >= k_max_instances) {
			continue;
		}
		instances[m_shadow_instance_count] =
		    makeVoxelInstance(proxy.model, proxy.inverse_model, proxy.previous_model, proxy.has_previous, proxy.record_index);
		bounds[m_shadow_instance_count] = glm::vec4(proxy.bounds_center, proxy.bounds_radius);
		++m_shadow_instance_count;
	}

	allocation.flush(0, sizeof(VoxelInstanceGpu) * m_shadow_instance_count);
	if (bounds != nullptr) {
		m_instance_bounds[frame_index].getAllocation().flush(0, sizeof(glm::vec4) * m_shadow_instance_count);
		if (m_ambient_rays) {
			writeAmbientGrid(
			    frame_index,
			    std::span<const glm::vec4>(bounds, std::min(m_shadow_instance_count, VulkanRenderer::instance->voxelShadowInstances()))
			);
		}
	}
	prepareGlass(frame_index, *frame->voxel_storage, std::span<const VoxelInstanceGpu>(instances, m_draws.size()));

	// Before any scope of this frame binds the set since updating a bound set invalidates the command buffer
	m_scene_sets.updateTlas(frame_index);
	if (m_glass_count > 0) {
		m_glass_scene_sets.updateTlas(frame_index);
	}

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
			} else {
				if (m_occlusion_mode == OcclusionMode::idle && m_occlusion_idle_frames-- == 0) {
					m_occlusion_mode = OcclusionMode::engaged;
					m_occlusion_low_frames = 0;
				}
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
			barriers.buffer(**buffers.classes, sync::Usage::compute_storage_write, sync::Usage::compute_storage_read);
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
	const auto* renderer = VulkanRenderer::instance;
	const float hidden = static_cast<float>(retest_cells) / static_cast<float>(std::max(listed_cells, 1u));
	m_occlusion_low_frames = hidden < renderer->voxelOcclusionMinHidden() ? m_occlusion_low_frames + 1 : 0;
	if (m_occlusion_low_frames >= k_occlusion_low_frames) {
		m_occlusion_mode = OcclusionMode::idle;
		m_occlusion_idle_frames = renderer->voxelOcclusionIdleFrames();
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

void VoxelPass::recordLighting(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D viewport) {
	ZoneScoped;
	if (!m_lighting_pipeline.isReady() || frame_index >= m_gbuffer_sets.size() || frame_index >= m_lighting_storage_sets.size() ||
	    !m_lighting_scene_sets.get(frame_index)) {
		return;
	}

	const auto* instance = VulkanRenderer::instance;
	const auto* frame = instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	const std::array<vk::ImageView, 8> views {
	  instance->getVoxelAlbedoView(),
	  instance->getVoxelFaceNormalView(),
	  instance->getVoxelMaterialView(),
	  instance->getDepthView(),
	  instance->getVoxelPreviousReservoirView(),
	  instance->getSceneMotionView(),
	  instance->getVoxelCandidateReservoirView(),
	  instance->getSceneNormalView(),
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

	const auto height = static_cast<float>(viewport.height);
	const float rotation =
	    frame->taa_active ? std::fmod(static_cast<float>(frame->sequence % 4096) * k_golden_ratio_conjugate, 1.0f) : 0.0f;
	const LightingPushConstants push {
	  .inverse_view_projection = glm::inverse(frame->frame_data.jittered_view_projection),
	  .camera_radius = glm::vec4(frame->frame_data.camera_position, instance->voxelNormalRounding() * voxel::k_voxel_size),
	  .viewport = glm::vec4(
	      static_cast<float>(viewport.width), height, 0.5f * height * std::abs(frame->frame_data.projection[1][1]), rotation
	  ),
	  .lighting = glm::uvec4(
	      std::min(m_shadow_instance_count, instance->voxelShadowInstances()),
	      instance->voxelLightCandidates(),
	      !m_marched_lights && !m_ambient_rays ? k_marched_none : (m_denoise_active ? k_marched_into_terms : k_marched_into_colour),
	      instance->voxelSunReach()
	  ),
	  .reuse = glm::uvec4(
	      (instance->voxelReservoirHistory() ? k_reuse_history : 0u) | (m_spatial_active ? k_reuse_spatial : 0u),
	      k_reservoir_m_cap * instance->voxelLightCandidates(),
	      k_reused_reservoirs,
	      m_ambient_rays ? static_cast<uint32_t>(std::lround(instance->voxelAmbientReach() * 100.0f)) : 0u
	  ),
	};
	const vk::PipelineLayout layout = *m_lighting_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_lighting_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_scene_set,
	    std::array<vk::DescriptorSet, 3> {
	      m_lighting_scene_sets.get(frame_index), *m_lighting_storage_sets[frame_index], *m_gbuffer_sets[frame_index]
	    },
	    {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(LightingPushConstants), &push);
	m_lighting_push = push;
	cmd.draw(3, 1, 0, 0);
}

void VoxelPass::prepareGlass(uint32_t frame_index, const VoxelGpuStorage& storage, std::span<const VoxelInstanceGpu> instances) {
	ZoneScoped;
	m_glass_draws.clear();
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (!m_glass_pipeline.isReady() || frame_index >= m_glass_buffers.size() || frame == nullptr) {
		return;
	}

	const glm::vec3 camera = frame->frame_data.camera_position;
	const float half_brick = 0.5f * static_cast<float>(voxel::k_brick_dim);
	for (uint32_t i = 0; i < instances.size(); ++i) {
		const VoxelInstanceGpu& instance = instances[i];
		const std::span<const uint32_t> bricks = storage.glassBricks(instance.record_index);
		if (bricks.empty()) {
			continue;
		}
		const glm::uvec3 dims = storage.recordBrickDims(instance.record_index);
		for (const uint32_t brick : bricks) {
			const glm::uvec3 coord(brick % dims.x, (brick / dims.x) % dims.y, brick / (dims.x * dims.y));
			const glm::vec3 local = (glm::vec3(coord) * static_cast<float>(voxel::k_brick_dim)) + half_brick;
			const glm::vec3 delta = glm::vec3(instance.voxel_to_world * glm::vec4(local, 1.0f)) - camera;
			m_glass_draws.push_back({.distance_squared = glm::dot(delta, delta), .instance = i, .brick = brick});
		}
	}
	if (m_glass_draws.empty()) {
		return;
	}

	// Far to near and the nearest kept when over the limit
	std::ranges::sort(m_glass_draws, [](const GlassDraw& a, const GlassDraw& b) {
		return a.distance_squared > b.distance_squared;
	});
	size_t first = 0;
	if (m_glass_draws.size() > k_max_glass_bricks) {
		first = m_glass_draws.size() - k_max_glass_bricks;
		if (const auto now = std::chrono::steady_clock::now(); now - m_last_glass_warning > std::chrono::seconds(5)) {
			m_last_glass_warning = now;
			TOAST_WARN("Render", "VoxelPass: {} glass bricks, only the nearest {} are drawn", m_glass_draws.size(), k_max_glass_bricks);
		}
	}

	const auto& allocation = m_glass_buffers[frame_index].getAllocation();
	auto* mapped = static_cast<glm::uvec2*>(allocation.getInfo().pMappedData);
	if (mapped == nullptr) {
		return;
	}
	m_glass_draws.erase(m_glass_draws.begin(), m_glass_draws.begin() + static_cast<std::ptrdiff_t>(first));
	m_glass_count = static_cast<uint32_t>(m_glass_draws.size());
	for (uint32_t i = 0; i < m_glass_count; ++i) {
		mapped[i] = glm::uvec2(m_glass_draws[i].instance, m_glass_draws[i].brick);
	}
	allocation.flush(0, sizeof(glm::uvec2) * m_glass_count);
}

void VoxelPass::recordGlass(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t first, uint32_t count) {
	ZoneScoped;
	recordGlassBricks(cmd, frame_index, m_glass_pipeline, first, count);
}

void VoxelPass::recordGlassSurfaces(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	recordGlassBricks(cmd, frame_index, m_glass_surface_pipeline, 0, m_glass_count);
}

void VoxelPass::recordGlassBricks(
    vk::CommandBuffer cmd, uint32_t frame_index, const VulkanPipeline& pipeline, uint32_t first, uint32_t count
) {
	if (count == 0 || first + count > m_glass_count || !pipeline.isReady() || frame_index >= m_glass_sets.size() ||
	    !m_glass_scene_sets.get(frame_index)) {
		return;
	}

	const vk::PipelineLayout layout = *m_glass_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_scene_set,
	    std::array<vk::DescriptorSet, 2> {m_glass_scene_sets.get(frame_index), *m_glass_sets[frame_index]},
	    {}
	);
	// SV_InstanceID leaves out firstInstance so the run start is pushed
	const GlassPush push {.first = first};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(GlassPush), &push);
	cmd.draw(36, count, 0, 0);
}

void VoxelPass::writeFreshRegions(uint32_t frame_index, const VoxelGpuStorage& storage) {
	if (frame_index >= m_fresh_regions.size()) {
		return;
	}
	auto* mapped = static_cast<glm::vec4*>(m_fresh_regions[frame_index].getAllocation().getInfo().pMappedData);
	if (mapped == nullptr) {
		return;
	}

	// Only the first frame of a new storage marks its changes
	uint32_t count = 0;
	const uint64_t generation = storage.generation();
	if (generation != m_region_generation) {
		if (m_region_generation != 0) {
			(void)storage.changeHistory().forEachSince(m_region_generation, [&](const VoxelRegion& region) {
				const std::optional<uint32_t> record = storage.recordIndexOf(region.node_rid);
				if (!record.has_value() || count >= k_max_fresh_regions) {
					return;
				}
				mapped[1 + (count * 2)] = glm::vec4(region.min, static_cast<float>(*record));
				mapped[2 + (count * 2)] = glm::vec4(region.max, 0.0f);
				++count;
			});
		}
		m_region_generation = generation;
	}
	mapped[0] = glm::vec4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);
	m_fresh_regions[frame_index].getAllocation().flush(0, sizeof(glm::vec4) * (1 + (2 * count)));
}

void VoxelPass::createDenoise(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent) {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/voxel_denoise.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_WARN(
		    "Render", "VoxelPass shader core://shaders/voxel_denoise.slang unavailable; marched light is only smoothed by TAA"
		);
		return;
	}
	if (core.getPhysicalDevice().getProperties().limits.maxPushConstantsSize < sizeof(DenoisePush)) {
		TOAST_WARN(
		    "Render", "VoxelDenoise needs {} bytes of push constants; marched light is only smoothed by TAA", sizeof(DenoisePush)
		);
		return;
	}

	m_denoise_layout.rebuild(core, shader->reflection, "VoxelDenoise");
	const auto& layouts = m_denoise_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_WARN("Render", "VoxelDenoise shader layout has no descriptor sets; marched light is only smoothed by TAA");
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "VoxelDenoise Temporal";
	config.color_format = k_denoise_history_format;
	config.write_color_alpha = true;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.fragment_entry = "temporalMain";
	config.pipeline_layout = *m_denoise_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	m_denoise_temporal_pipeline.rebuild(core, config);

	config.debug_name = "VoxelDenoise Composite";
	config.color_format = scene_format;
	config.write_color_alpha = false;
	config.fragment_entry = "compositeMain";
	config.blend_preset = VulkanPipeline::BlendPreset::additive;
	m_denoise_composite_pipeline.rebuild(core, config);

	m_linear_sampler = vk::raii::Sampler(core.getDevice(), linearClampSamplerInfo());
	setDebugName(core, *m_linear_sampler, "VoxelDenoise LinearSampler");

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout set_layout = *layouts[0];
	m_denoise_sets.clear();
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight * 2; ++i) {
		auto allocated = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
		m_denoise_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_denoise_sets.back(), std::format("VoxelDenoise Set[{}]", i));
	}
	createDenoiseTargets(extent);
}

void VoxelPass::createDenoiseTargets(vk::Extent2D extent) {
	for (uint32_t term = 0; term < m_denoise_history.size(); ++term) {
		for (uint32_t parity = 0; parity < 2; ++parity) {
			m_denoise_history[term][parity].create(
			    *m_core, extent, k_denoise_history_format, std::format("VoxelDenoise {}{}", term == 0 ? "Diffuse" : "Specular", parity)
			);
		}
	}
	m_denoise_bound.assign(m_denoise_sets.size(), {});
	m_denoise_valid = false;
}

void VoxelPass::bindDenoise(vk::CommandBuffer cmd, uint32_t frame_index, const VulkanPipeline& pipeline, float term) {
	const auto* renderer = VulkanRenderer::instance;
	const auto* frame = renderer->renderingFrame();
	const uint32_t set_index = (frame_index * 2) + m_denoise_parity;
	const vk::DescriptorSet set = *m_denoise_sets[set_index];
	const uint32_t read = 1 - m_denoise_parity;

	const std::array<vk::ImageView, k_denoise_bindings> views {
	  renderer->getVoxelNoisyDiffuseView(),
	  renderer->getVoxelNoisySpecularView(),
	  renderer->getSceneMotionView(),
	  renderer->getDepthView(),
	  renderer->getVoxelAlbedoView(),
	  renderer->getVoxelFaceNormalView(),
	  m_denoise_history[0][read].view(),
	  m_denoise_history[1][read].view(),
	  m_denoise_history[0][m_denoise_parity].view(),
	  m_denoise_history[1][m_denoise_parity].view(),
	};

	// Safe since the fence of this slot was waited on
	if (m_denoise_bound[set_index] != views) {
		DescriptorWriter writer;
		for (uint32_t binding = 0; binding < views.size(); ++binding) {
			const bool history = binding == 6 || binding == 7;
			writer.image(
			    set,
			    binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    history ? *m_linear_sampler : *m_point_sampler,
			    views[binding],
			    binding == 3 ? vk::ImageLayout::eDepthReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal
			);
		}
		writer.flush(m_core->getDevice());
		m_denoise_bound[set_index] = views;
	}

	const vk::Extent2D extent = m_denoise_history[0][0].extent();
	const float rotation = std::fmod(static_cast<float>(frame->sequence % 4096) * k_golden_ratio_conjugate, 1.0f);
	const DenoisePush push {
	  .inverse_view_projection = glm::inverse(frame->frame_data.jittered_view_projection),
	  .previous_view_projection = frame->frame_data.previous_view_projection,
	  .params = glm::vec4(term, m_denoise_frame_valid ? 1.0f : 0.0f, rotation, k_denoise_radius),
	  .viewport = glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 0.0f),
	};

	const vk::PipelineLayout layout = *m_denoise_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getPipeline());
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, std::array<vk::DescriptorSet, 1> {set}, {});
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(DenoisePush), &push);
}

void VoxelPass::recordDenoiseTemporal(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (!m_denoise_active || frame == nullptr) {
		return;
	}

	// A skipped frame or a recreated target leaves a history from somewhere else
	m_denoise_frame_valid = m_denoise_valid && frame->sequence == m_denoise_sequence + 1;
	const uint32_t read = 1 - m_denoise_parity;
	if (!m_denoise_frame_valid) {
		m_denoise_history[0][read].makeSampleable(cmd);
		m_denoise_history[1][read].makeSampleable(cmd);
	}

	for (uint32_t term = 0; term < 2; ++term) {
		const PostProcessTarget& target = m_denoise_history[term][m_denoise_parity];
		target.beginScope(cmd);
		bindDenoise(cmd, frame_index, m_denoise_temporal_pipeline, static_cast<float>(term));
		cmd.draw(3, 1, 0, 0);
		target.endScope(cmd);
	}
}

void VoxelPass::recordDenoiseComposite(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (!m_denoise_active || frame == nullptr) {
		return;
	}
	bindDenoise(cmd, frame_index, m_denoise_composite_pipeline, 0.0f);
	cmd.draw(3, 1, 0, 0);

	m_denoise_parity = 1 - m_denoise_parity;
	m_denoise_valid = true;
	m_denoise_sequence = frame->sequence;
}

void VoxelPass::recordSpatial(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D viewport) {
	ZoneScoped;
	(void)viewport;
	if (!m_spatial_active || frame_index >= m_gbuffer_sets.size() || frame_index >= m_lighting_storage_sets.size() ||
	    !m_lighting_scene_sets.get(frame_index)) {
		return;
	}
	const vk::PipelineLayout layout = *m_lighting_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_spatial_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_scene_set,
	    std::array<vk::DescriptorSet, 3> {
	      m_lighting_scene_sets.get(frame_index), *m_lighting_storage_sets[frame_index], *m_gbuffer_sets[frame_index]
	    },
	    {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(LightingPushConstants), &m_lighting_push);
	cmd.draw(3, 1, 0, 0);
}

void VoxelPass::onTargetsRecreated() {
	std::ranges::fill(m_bound_gbuffer, std::array<vk::ImageView, 8> {});
	if (!m_denoise_sets.empty()) {
		createDenoiseTargets(VulkanRenderer::instance->getOutputTarget().getExtent());
	}

	if (!m_cell_sets.empty()) {
		const auto* renderer = VulkanRenderer::instance;
		m_hiz.create(*m_core, renderer->getDepthView(), renderer->getOutputTarget().getExtent());
		writeHizDescriptors();
	}
}

}
