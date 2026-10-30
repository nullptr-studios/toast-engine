/// @file gi_pass.cpp
/// @author dario
/// @date 22/10/2026

#include "toast/renderer/passes/lighting/gi_pass.hpp"

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
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_group = 64;
constexpr uint32_t k_pixel_group = 8;
/// Mirrors gi_common GiProbe
constexpr vk::DeviceSize k_probe_bytes = 32;
/// LOD start shortened by this share so neighbouring LODs overlap and blend
constexpr float k_lod_overlap = 0.9f;
/// LOD distance over probe spacing keeping a carried shell inside the 512 cell key window
constexpr float k_max_cells_to_lod = 80.0f;

constexpr uint32_t k_gi_depth_binding = 0;
constexpr uint32_t k_frame_binding = 1;
constexpr uint32_t k_keys_binding = 2;
constexpr uint32_t k_flags_binding = 3;
constexpr uint32_t k_previous_binding = 4;
constexpr uint32_t k_values_binding = 5;
constexpr uint32_t k_gi_cells_binding = 6;
constexpr uint32_t k_pool_binding = 7;
constexpr uint32_t k_counters_binding = 8;
constexpr uint32_t k_normal_binding = 9;
constexpr uint32_t k_deposit_pool_binding = 10;
constexpr uint32_t k_parent_binding = 11;
constexpr uint32_t k_ray_counts_binding = 12;
constexpr uint32_t k_deposits_binding = 13;
constexpr uint32_t k_deposit_slots_binding = 14;
constexpr uint32_t k_neighbours_binding = 15;
constexpr uint32_t k_history_source_binding = 16;
constexpr uint32_t k_dirty_binding = 17;
constexpr uint32_t k_points_binding = 18;
constexpr uint32_t k_spawn_binding = 19;

/// gi_debug
constexpr uint32_t k_debug_source_binding = 0;
constexpr uint32_t k_debug_depth_binding = 1;
constexpr uint32_t k_debug_frame_binding = 2;
constexpr uint32_t k_debug_keys_binding = 3;
constexpr uint32_t k_debug_values_binding = 4;
constexpr uint32_t k_debug_pool_binding = 5;
constexpr uint32_t k_debug_normal_binding = 6;
constexpr uint32_t k_debug_deposits_binding = 7;
constexpr uint32_t k_debug_deposit_slots_binding = 8;
constexpr uint32_t k_debug_irradiance_binding = 9;
constexpr uint32_t k_debug_history_binding = 10;
constexpr uint32_t k_debug_change_binding = 11;

/// gi_rays
constexpr uint32_t k_ray_scene_set = 0;
constexpr uint32_t k_ray_storage_set = 1;
constexpr uint32_t k_ray_gi_set = 2;
constexpr uint32_t k_ray_instances_binding = 6;
constexpr uint32_t k_ray_trace_volumes_binding = 10;
constexpr uint32_t k_ray_trace_bounds_binding = 11;
constexpr uint32_t k_ray_trace_grid_binding = 12;
constexpr uint32_t k_ray_trace_brick_bases_binding = 14;
constexpr uint32_t k_ray_trace_bricks_binding = 15;
constexpr uint32_t k_ray_depth_binding = 0;
constexpr uint32_t k_ray_normal_binding = 1;
constexpr uint32_t k_ray_frame_binding = 2;
constexpr uint32_t k_ray_keys_binding = 3;
constexpr uint32_t k_ray_values_binding = 4;
constexpr uint32_t k_ray_pool_binding = 5;
constexpr uint32_t k_ray_ray_counts_binding = 6;
constexpr uint32_t k_ray_deposits_binding = 7;
constexpr uint32_t k_ray_parent_binding = 8;
constexpr uint32_t k_ray_deposit_slots_binding = 9;
constexpr uint32_t k_ray_scene_colour_binding = 10;
constexpr uint32_t k_ray_counters_binding = 11;
constexpr uint32_t k_ray_points_binding = 12;
constexpr uint32_t k_ray_irradiance_binding = 13;
constexpr uint32_t k_ray_change_binding = 14;
constexpr uint32_t k_ray_spawn_binding = 15;
constexpr uint32_t k_ray_hits_binding = 16;
/// Mirrors gi_common GiPoint
constexpr vk::DeviceSize k_point_bytes = 16;
/// Mirrors gi_common GiHistory
constexpr vk::DeviceSize k_history_bytes = 12;

/// gi_merge
constexpr uint32_t k_merge_scene_set = 0;
constexpr uint32_t k_merge_set = 1;
constexpr uint32_t k_merge_frame_binding = 0;
constexpr uint32_t k_merge_pool_binding = 1;
constexpr uint32_t k_merge_deposit_pool_binding = 2;
constexpr uint32_t k_merge_deposit_slots_binding = 3;
constexpr uint32_t k_merge_neighbours_binding = 4;
constexpr uint32_t k_merge_deposits_binding = 5;
constexpr uint32_t k_merge_merged_binding = 6;
constexpr uint32_t k_merge_irradiance_binding = 7;
constexpr uint32_t k_merge_counters_binding = 8;
constexpr uint32_t k_merge_history_binding = 9;
constexpr uint32_t k_merge_history_source_binding = 10;
constexpr uint32_t k_merge_dirty_binding = 11;

/// Rays of history a probe keeps after a change crosses its interval
constexpr float k_reset_history = 0.25f;

/// Mirrors gi_common kGiBaseDirections
constexpr uint32_t k_base_directions = 4;
/// Mirrors gi_common kGiStrideThreads
constexpr uint32_t k_stride_groups = 4096;
constexpr vk::Format k_atlas_format = vk::Format::eR16G16B16A16Sfloat;
/// First interval length t0 in c0 probe spacings
constexpr float k_interval_scale = 1.6f;
/// 1/256 steps so a bucket holds about a thousand rays at gi_rays kGiMaxRadiance
constexpr float k_deposit_fixed_scale = 256.0f;

auto groups(uint32_t count, uint32_t size) -> uint32_t {
	return (count + size - 1) / size;
}

auto deviceBuffer(const VulkanCore& core, vk::DeviceSize size, vk::BufferUsageFlags usage, std::string_view name)
    -> vma::raii::Buffer {
	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = size;
	buffer_ci.usage = usage;
	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	vma::raii::Buffer buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *buffer, name);
	return buffer;
}

auto hostBuffer(const VulkanCore& core, vk::DeviceSize size, vk::BufferUsageFlags usage, bool read, std::string_view name)
    -> vma::raii::Buffer {
	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = size;
	buffer_ci.usage = usage;
	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = read ? vma::MemoryUsage::eAutoPreferHost : vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | (read ? vma::AllocationCreateFlagBits::eHostAccessRandom
	                                                                : vma::AllocationCreateFlagBits::eHostAccessSequentialWrite);
	vma::raii::Buffer buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *buffer, name);
	return buffer;
}

void computeBarrier(vk::CommandBuffer cmd) {
	sync::BarrierBatch barriers;
	barriers.memory(sync::Usage::compute_storage_write, sync::Usage::compute_storage_write);
	barriers.flush(cmd);
}

/// Mirrors gi_common giRayPoint fixed point R2
auto r2Jitter(uint32_t frame) -> glm::vec2 {
	const uint32_t x = 0x80000000u + (frame * 3242174889u);
	const uint32_t y = 0x80000000u + (frame * 2447445414u);
	return glm::vec2(static_cast<float>(x >> 8), static_cast<float>(y >> 8)) / 16777216.0f;
}

struct QualityLevel {
	uint32_t ray_budget = 0;
	uint32_t cascades = 0;
	float spacing = 0.0f;
	float lod_distance = 0.0f;
	bool multibounce = false;
	uint32_t points = 0;
	uint32_t interval = 1;
};

/// Scalability
constexpr std::array<QualityLevel, GiSettings::k_quality_custom> k_quality_levels {
  {
   {.ray_budget = 150'000,
	   .cascades = 4,
	   .spacing = 0.2f,
	   .lod_distance = 4.0f,
	   .multibounce = false,
	   .points = 8192,
	   .interval = 2},
   {.ray_budget = 300'000,
	   .cascades = 4,
	   .spacing = 0.15f,
	   .lod_distance = 4.0f,
	   .multibounce = true,
	   .points = 32768,
	   .interval = 1},
   {.ray_budget = 600'000,
	   .cascades = 5,
	   .spacing = 0.1f,
	   .lod_distance = 4.0f,
	   .multibounce = true,
	   .points = 65536,
	   .interval = 1},
   {.ray_budget = 2'200'000,
	   .cascades = 5,
	   .spacing = 0.1f,
	   .lod_distance = 4.0f,
	   .multibounce = true,
	   .points = 65536,
	   .interval = 1},
   }
};

auto stepForBudget(vk::Extent2D extent, uint32_t budget) -> uint32_t {
	for (uint32_t step = 1; step < 4; ++step) {
		const uint64_t rays = static_cast<uint64_t>(groups(extent.width, step)) * groups(extent.height, step);
		if (rays <= budget) {
			return step;
		}
	}
	return 4;
}

/// Most pixels any quality preset spawns at this extent
auto spawnCapacity(vk::Extent2D extent) -> uint32_t {
	uint32_t capacity = 1;
	for (const QualityLevel& preset : k_quality_levels) {
		const uint32_t step = stepForBudget(extent, preset.ray_budget);
		capacity = std::max(capacity, groups(extent.width, step) * groups(extent.height, step));
	}
	return capacity;
}

}

auto GiPass::resolveQuality() const -> Quality {
	const GiSettings& settings = VulkanRenderer::instance->giSettings();
	constexpr auto relaxed = std::memory_order_relaxed;
	const uint32_t level = std::min(settings.quality.load(relaxed), GiSettings::k_quality_custom);
	if (level == GiSettings::k_quality_custom) {
		uint32_t step = std::clamp(settings.ray_step.load(relaxed), 1u, 4u);
		// A step whose pixels overflow the spawn list would starve the screen tail
		while (m_spawn_capacity > 0 && step < 4 && groups(m_extent.width, step) * groups(m_extent.height, step) > m_spawn_capacity) {
			++step;
		}
		return Quality {
		  .level = level,
		  .step = step,
		  .cascades = std::clamp(settings.cascades.load(relaxed), 1u, k_max_cascades),
		  .spacing = settings.probe_spacing.load(relaxed),
		  .lod_distance = settings.lod_distance.load(relaxed),
		  .multibounce = settings.multibounce.load(relaxed),
		  .points = std::clamp(settings.secondary_points.load(relaxed), 1u, k_max_points),
		  .interval = std::clamp(settings.update_interval.load(relaxed), 1u, 4u),
		};
	}
	const QualityLevel& preset = k_quality_levels[level];
	return Quality {
	  .level = level,
	  .step = stepForBudget(m_extent, preset.ray_budget),
	  .cascades = std::min(preset.cascades, k_max_cascades),
	  .spacing = preset.spacing,
	  .lod_distance = preset.lod_distance,
	  .multibounce = preset.multibounce,
	  .points = std::min(preset.points, k_max_points),
	  .interval = preset.interval,
	};
}

GiPass::GiPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format),
      m_extent(extent) {
	ZoneScoped;
	static_assert(sizeof(FrameData) == 416, "FrameData is mirrored by gi_common GiFrame");

	createLayout();
	createBuffers(core);
	if (!createPipelines(core)) {
		setEnabled(false);
		return;
	}
	m_debug_target.create(core, extent, scene_format, "GiPass Debug");
	TOAST_INFO(
	    "Render",
	    "GiPass ready ({} table entries, {} probes per pool, {} deposit probes, {} MB of deposits history and merged radiance)",
	    m_table_total,
	    m_pool_total,
	    m_deposit_total,
	    ((sizeof(glm::uvec4) + (2 * k_history_bytes) + (sizeof(glm::uvec2) / 4)) * k_deposit_slots) >> 20
	);
}

void GiPass::createLayout() {
	uint32_t table = 0;
	uint32_t pool = 0;
	uint32_t deposit = 0;
	for (uint32_t i = 0; i < k_max_cascades; ++i) {
		const uint32_t capacity = std::max(k_base_capacity >> (2 * i), k_min_capacity);
		// Half full at most so linear probing stays short
		const uint32_t size = std::bit_ceil(capacity * 2);
		const uint32_t side = k_base_directions << i;
		const uint32_t directions = 2 * side * side;
		// c0 deposit probes double as irradiance tiles
		const uint32_t deposit_capacity = i == 0 ? std::min(capacity, k_atlas_tiles) : capacity;
		m_cascades[i] = Cascade {
		  .table_offset = table,
		  .table_size = size,
		  .pool_offset = pool,
		  .capacity = capacity,
		  .deposit_offset = deposit,
		  .deposit_capacity = deposit_capacity,
		  .directions = directions,
		};
		table += size;
		pool += capacity;
		deposit += deposit_capacity;
	}
	m_table_total = table;
	m_pool_total = pool;
	m_deposit_total = deposit;
}

void GiPass::createBuffers(const VulkanCore& core) {
	ZoneScoped;
	constexpr auto storage = vk::BufferUsageFlagBits::eStorageBuffer;
	m_table_keys.emplace(deviceBuffer(core, sizeof(uint32_t) * m_table_total, storage, "GiPass TableKeys"));
	m_table_flags.emplace(deviceBuffer(core, sizeof(uint32_t) * m_table_total, storage, "GiPass TableFlags"));
	m_table_previous.emplace(deviceBuffer(core, sizeof(uint32_t) * m_table_total, storage, "GiPass TablePrevious"));
	m_table_values.emplace(deviceBuffer(core, sizeof(uint32_t) * m_table_total, storage, "GiPass TableValues"));
	m_table_cells.emplace(deviceBuffer(core, sizeof(glm::ivec4) * m_table_total, storage, "GiPass TableCells"));
	m_pool.emplace(deviceBuffer(core, k_probe_bytes * 2 * m_pool_total, storage, "GiPass Pool"));
	m_counters.emplace(deviceBuffer(
	    core,
	    sizeof(uint32_t) * k_counter_words,
	    storage | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
	    "GiPass Counters"
	));
	m_deposit_pool.emplace(deviceBuffer(core, sizeof(uint32_t) * m_deposit_total, storage, "GiPass DepositPool"));
	m_parent.emplace(deviceBuffer(core, sizeof(uint32_t) * m_deposit_total, storage, "GiPass Parent"));
	m_ray_counts.emplace(deviceBuffer(core, sizeof(glm::uvec4) * m_deposit_total, storage, "GiPass RayCounts"));
	m_deposits.emplace(deviceBuffer(core, sizeof(glm::uvec4) * k_deposit_slots, storage, "GiPass Deposits"));
	m_deposit_slots.emplace(deviceBuffer(core, sizeof(uint32_t) * m_deposit_total, storage, "GiPass DepositSlots"));
	m_neighbours.emplace(deviceBuffer(core, sizeof(glm::uvec4) * 2 * m_deposit_total, storage, "GiPass Neighbours"));
	// Every probe direction count is a multiple of four so slot / 4 indexes the averaged directions
	m_merged.emplace(deviceBuffer(core, sizeof(glm::uvec2) * (k_deposit_slots / 4), storage, "GiPass Merged"));

	constexpr uint32_t atlas_width = k_atlas_tiles_per_row * 8;
	constexpr uint32_t atlas_height = k_atlas_rows * 8;
	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = k_atlas_format;
	image_ci.extent = vk::Extent3D {atlas_width, atlas_height, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 1;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;
	vma::AllocationCreateInfo image_alloc {};
	image_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_atlas.emplace(core.getAllocator().createImage(image_ci, image_alloc));
	setDebugName(core, **m_atlas, "GiPass IrradianceAtlas");
	const vk::ImageSubresourceRange range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	m_atlas_state.reset(**m_atlas, range);

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_atlas;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = k_atlas_format;
	view_ci.subresourceRange = range;
	m_atlas_view.emplace(core.getDevice(), view_ci);
	setDebugName(core, **m_atlas_view, "GiPass IrradianceAtlas View");

	m_published.emplace(
	    deviceBuffer(core, sizeof(FrameData), storage | vk::BufferUsageFlagBits::eTransferDst, "GiPass PublishedFrame")
	);
	m_history.emplace(deviceBuffer(core, k_history_bytes * 2 * k_deposit_slots, storage, "GiPass History"));
	m_history_source.emplace(deviceBuffer(core, sizeof(glm::uvec2) * m_deposit_total, storage, "GiPass HistorySource"));
	m_points.emplace(deviceBuffer(core, k_point_bytes * 2 * k_max_points, storage, "GiPass Points"));

	// Lighting binds the atlas and the published frame before the first GI frame so both start readable and empty
	const auto& device = core.getDevice();
	const vk::raii::CommandPool one_shot_pool(
	    device, vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, core.getGraphicsQueueFamilyIndex())
	);
	auto cmd_buffers =
	    device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(*one_shot_pool, vk::CommandBufferLevel::ePrimary, 1));
	const vk::raii::CommandBuffer cmd = std::move(cmd_buffers[0]);
	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	cmd.fillBuffer(**m_counters, 0, vk::WholeSize, 0u);
	cmd.fillBuffer(**m_published, 0, vk::WholeSize, 0u);
	recordUndefinedToTransferDst(cmd, **m_atlas, range);
	cmd.clearColorImage(**m_atlas, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f), range);
	recordTransferDstToShaderRead(cmd, **m_atlas, range);
	sync::BarrierBatch barriers;
	barriers.memory(sync::Usage::transfer_dst, sync::Usage::general);
	barriers.flush(*cmd);
	cmd.end();
	submitAndWait(device, core.getGraphicsQueue(), core.graphicsSubmitMutex(), *cmd);
	m_atlas_state.usage = sync::Usage::shader_sampled;

	m_frame_data.clear();
	m_readback.clear();
	m_dirty.clear();
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		m_dirty.push_back(
		    hostBuffer(core, sizeof(glm::vec4) * (1 + (2 * k_max_dirty)), storage, false, std::format("GiPass Dirty[{}]", i))
		);
		const auto& dirty_allocation = m_dirty.back().getAllocation();
		std::memset(dirty_allocation.getInfo().pMappedData, 0, sizeof(glm::vec4));
		dirty_allocation.flush(0, sizeof(glm::vec4));
		m_frame_data.push_back(hostBuffer(
		    core, sizeof(FrameData), storage | vk::BufferUsageFlagBits::eTransferSrc, false, std::format("GiPass Frame[{}]", i)
		));
		m_readback.push_back(hostBuffer(
		    core,
		    sizeof(uint32_t) * k_counter_words,
		    vk::BufferUsageFlagBits::eTransferDst,
		    true,
		    std::format("GiPass Readback[{}]", i)
		));
	}
	m_dirty_stats.assign(VulkanRenderer::k_frames_in_flight, Stats {});
	m_readback_valid.assign(VulkanRenderer::k_frames_in_flight, false);
	m_readback_parity.assign(VulkanRenderer::k_frames_in_flight, 0u);
	m_readback_point_share.assign(VulkanRenderer::k_frames_in_flight, m_point_share);
	m_readback_point_budget.assign(VulkanRenderer::k_frames_in_flight, k_max_points);
	m_readback_spawn_capacity.assign(VulkanRenderer::k_frames_in_flight, 0u);

	m_point_sampler = vk::raii::Sampler(core.getDevice(), nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "GiPass PointSampler");
	m_linear_sampler = vk::raii::Sampler(core.getDevice(), linearClampSamplerInfo());
	setDebugName(core, *m_linear_sampler, "GiPass LinearSampler");
}

auto GiPass::createPipelines(const VulkanCore& core) -> bool {
	ZoneScoped;
	const auto probe_uid = assets::resolveURI("core://shaders/gi_probes.slang");
	const auto probe_shader = probe_uid.has_value() ? ShaderCache::get().acquire(*probe_uid) : nullptr;
	const auto debug_uid = assets::resolveURI("core://shaders/gi_debug.slang");
	const auto debug_shader = debug_uid.has_value() ? ShaderCache::get().acquire(*debug_uid) : nullptr;
	const auto ray_uid = assets::resolveURI("core://shaders/gi_rays.slang");
	const auto ray_shader = ray_uid.has_value() ? ShaderCache::get().acquire(*ray_uid) : nullptr;
	const auto merge_uid = assets::resolveURI("core://shaders/gi_merge.slang");
	const auto merge_shader = merge_uid.has_value() ? ShaderCache::get().acquire(*merge_uid) : nullptr;
	if (!probe_shader || !debug_shader || !ray_shader || !merge_shader) {
		TOAST_ERROR("Render", "GiPass shaders unavailable, radiance cascades are disabled");
		return false;
	}

	m_probe_layout.rebuild(core, probe_shader->reflection, "GiPass Probes");
	const auto compute = [&](VulkanPipeline& pipeline, const char* entry) {
		VulkanPipeline::Config config;
		config.pipeline_type = VulkanPipeline::PipelineType::compute;
		config.debug_name = std::format("GiPass {}", entry);
		config.shader_spirv = probe_shader->spirv;
		config.pipeline_layout = *m_probe_layout.getPipelineLayout();
		config.compute_entry = entry;
		pipeline.rebuild(core, config);
	};
	compute(m_clear_pipeline, "clearMain");
	compute(m_carry_pipeline, "carryMain");
	compute(m_pixel_pipeline, "pixelMain");
	compute(m_point_pipeline, "pointMain");
	compute(m_parent_pipeline, "parentMain");
	compute(m_compact_pipeline, "compactMain");
	compute(m_keep_pipeline, "keepMain");
	compute(m_slots_pipeline, "slotsMain");
	compute(m_deposit_clear_pipeline, "depositClearMain");
	compute(m_link_pipeline, "linkMain");
	compute(m_count_pipeline, "countMain");
	compute(m_point_count_pipeline, "pointCountMain");
	compute(m_propagate_pipeline, "propagateMain");
	compute(m_offset_pipeline, "offsetMain");

	m_ray_layout.rebuild(core, ray_shader->reflection, "GiPass Rays");
	VulkanPipeline::Config ray_config;
	ray_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	ray_config.debug_name = "GiPass traceMain";
	ray_config.shader_spirv = ray_shader->spirv;
	ray_config.pipeline_layout = *m_ray_layout.getPipelineLayout();
	ray_config.compute_entry = "traceMain";
	m_ray_pipeline.rebuild(core, ray_config);
	ray_config.debug_name = "GiPass pointTraceMain";
	ray_config.compute_entry = "pointTraceMain";
	m_point_trace_pipeline.rebuild(core, ray_config);
	ray_config.debug_name = "GiPass shadeMain";
	ray_config.compute_entry = "shadeMain";
	m_shade_pipeline.rebuild(core, ray_config);
	ray_config.debug_name = "GiPass changeMain";
	ray_config.compute_entry = "changeMain";
	m_change_pipeline.rebuild(core, ray_config);

	m_merge_layout.rebuild(core, merge_shader->reflection, "GiPass Merge");
	VulkanPipeline::Config merge_config;
	merge_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	merge_config.shader_spirv = merge_shader->spirv;
	merge_config.pipeline_layout = *m_merge_layout.getPipelineLayout();
	merge_config.debug_name = "GiPass mergeMain";
	merge_config.compute_entry = "mergeMain";
	m_merge_pipeline.rebuild(core, merge_config);
	merge_config.debug_name = "GiPass irradianceMain";
	merge_config.compute_entry = "irradianceMain";
	m_irradiance_pipeline.rebuild(core, merge_config);

	m_debug_layout.rebuild(core, debug_shader->reflection, "GiPass Debug");
	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "GiPass Debug";
	config.color_format = m_scene_format;
	config.write_color_alpha = true;
	config.extent = m_extent;
	config.shader_spirv = debug_shader->spirv;
	config.pipeline_layout = *m_debug_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	m_debug_pipeline.rebuild(core, config);

	const auto& probe_layouts = m_probe_layout.getDescriptorSetLayouts();
	const auto& debug_layouts = m_debug_layout.getDescriptorSetLayouts();
	const auto& ray_layouts = m_ray_layout.getDescriptorSetLayouts();
	const auto& merge_layouts = m_merge_layout.getDescriptorSetLayouts();
	if (probe_layouts.empty() || debug_layouts.empty() || ray_layouts.size() <= k_ray_gi_set ||
	    merge_layouts.size() <= k_merge_set) {
		TOAST_ERROR("Render", "GiPass shader layouts have no descriptor sets, radiance cascades are disabled");
		return false;
	}

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout probe_set_layout = *probe_layouts[0];
	const vk::DescriptorSetLayout debug_set_layout = *debug_layouts[0];
	const vk::DescriptorSetLayout ray_storage_layout = *ray_layouts[k_ray_storage_set];
	const vk::DescriptorSetLayout ray_set_layout = *ray_layouts[k_ray_gi_set];
	const vk::DescriptorSetLayout merge_set_layout = *merge_layouts[k_merge_set];
	m_ray_scene_sets.create(core, ray_shader->reflection, *ray_layouts[k_ray_scene_set], "GiPass Rays");
	m_merge_scene_sets.create(core, merge_shader->reflection, *merge_layouts[k_merge_scene_set], "GiPass Merge");
	m_ray_instances_bound = std::ranges::any_of(ray_shader->reflection.bindings, [](const ShaderBinding& binding) {
		return binding.set == k_ray_storage_set && binding.binding == k_ray_instances_binding;
	});
	const VoxelTraceScene* trace = VulkanRenderer::instance->voxelTraceScene();
	m_probe_sets.clear();
	m_debug_sets.clear();
	m_ray_storage_sets.clear();
	m_ray_sets.clear();
	m_merge_sets.clear();
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto probe = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &probe_set_layout));
		m_probe_sets.push_back(std::move(probe[0]));
		setDebugName(core, *m_probe_sets.back(), std::format("GiPass ProbeSet[{}]", i));
		auto debug = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &debug_set_layout));
		m_debug_sets.push_back(std::move(debug[0]));
		setDebugName(core, *m_debug_sets.back(), std::format("GiPass DebugSet[{}]", i));

		DescriptorWriter writer;
		const vk::DescriptorSet probe_set = *m_probe_sets.back();
		constexpr auto storage = vk::DescriptorType::eStorageBuffer;
		writer.buffer(probe_set, k_frame_binding, storage, *m_frame_data[i]);
		writer.buffer(probe_set, k_keys_binding, storage, **m_table_keys);
		writer.buffer(probe_set, k_flags_binding, storage, **m_table_flags);
		writer.buffer(probe_set, k_previous_binding, storage, **m_table_previous);
		writer.buffer(probe_set, k_values_binding, storage, **m_table_values);
		writer.buffer(probe_set, k_gi_cells_binding, storage, **m_table_cells);
		writer.buffer(probe_set, k_pool_binding, storage, **m_pool);
		writer.buffer(probe_set, k_counters_binding, storage, **m_counters);
		writer.buffer(probe_set, k_deposit_pool_binding, storage, **m_deposit_pool);
		writer.buffer(probe_set, k_parent_binding, storage, **m_parent);
		writer.buffer(probe_set, k_ray_counts_binding, storage, **m_ray_counts);
		writer.buffer(probe_set, k_deposits_binding, storage, **m_deposits);
		writer.buffer(probe_set, k_deposit_slots_binding, storage, **m_deposit_slots);
		writer.buffer(probe_set, k_neighbours_binding, storage, **m_neighbours);
		writer.buffer(probe_set, k_history_source_binding, storage, **m_history_source);
		writer.buffer(probe_set, k_dirty_binding, storage, *m_dirty[i]);
		writer.buffer(probe_set, k_points_binding, storage, **m_points);

		const vk::DescriptorSet debug_set = *m_debug_sets.back();
		writer.buffer(debug_set, k_debug_frame_binding, storage, *m_frame_data[i]);
		writer.buffer(debug_set, k_debug_keys_binding, storage, **m_table_keys);
		writer.buffer(debug_set, k_debug_values_binding, storage, **m_table_values);
		writer.buffer(debug_set, k_debug_pool_binding, storage, **m_pool);
		writer.buffer(debug_set, k_debug_deposits_binding, storage, **m_deposits);
		writer.buffer(debug_set, k_debug_deposit_slots_binding, storage, **m_deposit_slots);
		writer.buffer(debug_set, k_debug_history_binding, storage, **m_history);
		writer.image(
		    debug_set,
		    k_debug_irradiance_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_linear_sampler,
		    **m_atlas_view,
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);

		auto ray_storage = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &ray_storage_layout));
		m_ray_storage_sets.push_back(std::move(ray_storage[0]));
		setDebugName(core, *m_ray_storage_sets.back(), std::format("GiPass RayStorageSet[{}]", i));
		const vk::DescriptorSet ray_storage_set = *m_ray_storage_sets.back();
		if (trace != nullptr) {
			writer.buffer(ray_storage_set, k_ray_trace_volumes_binding, storage, trace->volumeBuffer(i));
			writer.buffer(ray_storage_set, k_ray_trace_bounds_binding, storage, trace->boundsBuffer(i));
			writer.buffer(ray_storage_set, k_ray_trace_grid_binding, storage, trace->gridBuffer(i));
			if (m_ray_instances_bound) {
				writer.buffer(ray_storage_set, k_ray_instances_binding, storage, trace->volumeBuffer(i));
			}
			if (ShaderCompiler::isRayQueryAvailable()) {
				const VoxelBlasCache* bricks = VulkanRenderer::instance->voxelBlasCache();
				writer.buffer(ray_storage_set, k_ray_trace_brick_bases_binding, storage, trace->brickBaseBuffer(i));
				writer.buffer(
				    ray_storage_set,
				    k_ray_trace_bricks_binding,
				    storage,
				    bricks != nullptr ? bricks->poolBuffer() : trace->brickBaseBuffer(i)
				);
			}
		}

		auto ray = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &ray_set_layout));
		m_ray_sets.push_back(std::move(ray[0]));
		setDebugName(core, *m_ray_sets.back(), std::format("GiPass RaySet[{}]", i));
		const vk::DescriptorSet ray_set = *m_ray_sets.back();
		writer.buffer(ray_set, k_ray_frame_binding, storage, *m_frame_data[i]);
		writer.buffer(ray_set, k_ray_keys_binding, storage, **m_table_keys);
		writer.buffer(ray_set, k_ray_values_binding, storage, **m_table_values);
		writer.buffer(ray_set, k_ray_pool_binding, storage, **m_pool);
		writer.buffer(ray_set, k_ray_ray_counts_binding, storage, **m_ray_counts);
		writer.buffer(ray_set, k_ray_deposits_binding, storage, **m_deposits);
		writer.buffer(ray_set, k_ray_parent_binding, storage, **m_parent);
		writer.buffer(ray_set, k_ray_deposit_slots_binding, storage, **m_deposit_slots);
		writer.buffer(ray_set, k_ray_counters_binding, storage, **m_counters);
		writer.buffer(ray_set, k_ray_points_binding, storage, **m_points);
		writer.image(
		    ray_set,
		    k_ray_irradiance_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_linear_sampler,
		    **m_atlas_view,
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);

		auto merge = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &merge_set_layout));
		m_merge_sets.push_back(std::move(merge[0]));
		setDebugName(core, *m_merge_sets.back(), std::format("GiPass MergeSet[{}]", i));
		const vk::DescriptorSet merge_set = *m_merge_sets.back();
		writer.buffer(merge_set, k_merge_frame_binding, storage, *m_frame_data[i]);
		writer.buffer(merge_set, k_merge_pool_binding, storage, **m_pool);
		writer.buffer(merge_set, k_merge_deposit_pool_binding, storage, **m_deposit_pool);
		writer.buffer(merge_set, k_merge_deposit_slots_binding, storage, **m_deposit_slots);
		writer.buffer(merge_set, k_merge_neighbours_binding, storage, **m_neighbours);
		writer.buffer(merge_set, k_merge_deposits_binding, storage, **m_deposits);
		writer.buffer(merge_set, k_merge_merged_binding, storage, **m_merged);
		writer.image(
		    merge_set,
		    k_merge_irradiance_binding,
		    vk::DescriptorType::eStorageImage,
		    nullptr,
		    **m_atlas_view,
		    vk::ImageLayout::eGeneral
		);
		writer.buffer(merge_set, k_merge_counters_binding, storage, **m_counters);
		writer.buffer(merge_set, k_merge_history_binding, storage, **m_history);
		writer.buffer(merge_set, k_merge_history_source_binding, storage, **m_history_source);
		writer.buffer(merge_set, k_merge_dirty_binding, storage, *m_dirty[i]);
		writer.flush(core.getDevice());
	}
	m_probe_bound.assign(VulkanRenderer::k_frames_in_flight, {});
	m_debug_bound.assign(VulkanRenderer::k_frames_in_flight, {});
	m_ray_bound.assign(VulkanRenderer::k_frames_in_flight, {});
	createChange(core);
	m_ray_storage_bound.assign(VulkanRenderer::k_frames_in_flight, nullptr);
	if (trace == nullptr) {
		TOAST_WARN("Render", "GiPass found no voxel trace scene so probes get no rays");
	}

	return m_clear_pipeline.isReady() && m_carry_pipeline.isReady() && m_pixel_pipeline.isReady() && m_parent_pipeline.isReady() &&
	       m_compact_pipeline.isReady() && m_keep_pipeline.isReady() && m_slots_pipeline.isReady() &&
	       m_deposit_clear_pipeline.isReady() && m_link_pipeline.isReady() && m_count_pipeline.isReady() &&
	       m_propagate_pipeline.isReady() && m_offset_pipeline.isReady() && m_point_pipeline.isReady() &&
	       m_point_count_pipeline.isReady() && m_merge_pipeline.isReady() && m_irradiance_pipeline.isReady();
}

void GiPass::onResize(vk::Extent2D extent) {
	m_extent = extent;
	if (m_core != nullptr) {
		m_debug_target.create(*m_core, extent, m_scene_format, "GiPass Debug");
		std::ranges::fill(m_probe_bound, std::array<vk::ImageView, 2> {});
		std::ranges::fill(m_debug_bound, std::array<vk::ImageView, 3> {});
		std::ranges::fill(m_ray_bound, std::array<vk::ImageView, 3> {});
		createChange(*m_core);
	}
}

void GiPass::createChange(const VulkanCore& core) {
	if (m_ray_sets.empty() || m_debug_sets.empty()) {
		return;
	}
	const vk::DeviceSize pixels = static_cast<vk::DeviceSize>(std::max(m_extent.width, 1u)) * std::max(m_extent.height, 1u);
	m_change.reset();
	m_change.emplace(deviceBuffer(core, sizeof(glm::vec4) * pixels, vk::BufferUsageFlagBits::eStorageBuffer, "GiPass Change"));
	m_spawn_capacity = spawnCapacity(m_extent);
	m_spawn.reset();
	m_spawn.emplace(
	    deviceBuffer(core, sizeof(uint32_t) * m_spawn_capacity, vk::BufferUsageFlagBits::eStorageBuffer, "GiPass Spawn")
	);
	m_hits.reset();
	m_hits.emplace(deviceBuffer(
	    core, sizeof(glm::uvec4) * (m_spawn_capacity + k_max_points), vk::BufferUsageFlagBits::eStorageBuffer, "GiPass Hits"
	));
	DescriptorWriter writer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		writer.buffer(*m_probe_sets[i], k_spawn_binding, vk::DescriptorType::eStorageBuffer, **m_spawn);
		writer.buffer(*m_ray_sets[i], k_ray_spawn_binding, vk::DescriptorType::eStorageBuffer, **m_spawn);
		writer.buffer(*m_ray_sets[i], k_ray_hits_binding, vk::DescriptorType::eStorageBuffer, **m_hits);
		writer.buffer(*m_ray_sets[i], k_ray_change_binding, vk::DescriptorType::eStorageBuffer, **m_change);
		writer.buffer(*m_debug_sets[i], k_debug_change_binding, vk::DescriptorType::eStorageBuffer, **m_change);
	}
	writer.flush(core.getDevice());
}

void GiPass::recordChange(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView scene_view) {
	ZoneScoped;
	if (!m_change_pipeline.isReady() || !m_change.has_value() || !bindRaySets(frame_index, scene_view)) {
		return;
	}
	const vk::PipelineLayout layout = *m_ray_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_change_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute,
	    layout,
	    0,
	    std::array {m_ray_scene_sets.get(frame_index), *m_ray_storage_sets[frame_index], *m_ray_sets[frame_index]},
	    {}
	);
	cmd.dispatch(groups(m_extent.width, k_pixel_group), groups(m_extent.height, k_pixel_group), 1);
}

void GiPass::gatherDirty() {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	const auto add = [&](glm::vec3 low, glm::vec3 high) {
		if (m_dirty_scratch.size() < 2 * k_max_dirty) {
			m_dirty_scratch.push_back(low);
			m_dirty_scratch.push_back(high);
			return;
		}
		auto last = m_dirty_scratch.end() - 2;
		last[0] = glm::min(last[0], low);
		last[1] = glm::max(last[1], high);
	};

	Stats& counts = m_dirty_pending;
	const float threshold = std::max(VulkanRenderer::instance->giSettings().motion_threshold.load(std::memory_order_relaxed), 0.0f);

	for (const auto& proxy : frame->voxel_instances) {
		if (proxy.has_previous && proxy.model == proxy.previous_model) {
			continue;
		}
		const glm::vec3 previous = glm::vec3(proxy.previous_model * proxy.inverse_model * glm::vec4(proxy.bounds_center, 1.0f));
		if (proxy.has_previous) {
			// Centre shift plus what the change of the linear part can move a point of the bounds
			const glm::mat3 change = glm::mat3(proxy.model) - glm::mat3(proxy.previous_model);
			const float spread =
			    std::sqrt(glm::dot(change[0], change[0]) + glm::dot(change[1], change[1]) + glm::dot(change[2], change[2]));
			const float scale = std::max(glm::length(glm::vec3(proxy.model[0])), 1e-6f);
			const float motion = glm::distance(proxy.bounds_center, previous) + (spread * proxy.bounds_radius / scale);
			counts.largest_motion = std::max(counts.largest_motion, motion);
			if (motion < threshold) {
				continue;
			}
			++counts.moved;
		} else {
			++counts.appeared;
		}
		const glm::vec3 radius(proxy.bounds_radius);
		add(glm::min(proxy.bounds_center, previous) - radius, glm::max(proxy.bounds_center, previous) + radius);
	}

	bool known = true;
	if (frame->voxel_storage) {
		const uint64_t generation = frame->voxel_storage->generation();
		if (generation != m_region_generation && m_region_generation != 0) {
			m_model_scratch.clear();
			for (const auto& proxy : frame->voxel_instances) {
				m_model_scratch.emplace(proxy.node_rid, proxy.model * glm::scale(glm::mat4(1.0f), glm::vec3(voxel::k_voxel_size)));
			}
			known = frame->voxel_storage->changeHistory().forEachSince(m_region_generation, [&](const VoxelRegion& region) {
				const auto found = m_model_scratch.find(region.node_rid);
				if (found == m_model_scratch.end()) {
					return;
				}
				++counts.edits;
				glm::vec3 low(std::numeric_limits<float>::max());
				glm::vec3 high(std::numeric_limits<float>::lowest());
				for (uint32_t corner = 0; corner < 8; ++corner) {
					const glm::vec3 local(
					    (corner & 1) != 0 ? region.max.x : region.min.x,
					    (corner & 2) != 0 ? region.max.y : region.min.y,
					    (corner & 4) != 0 ? region.max.z : region.min.z
					);
					const glm::vec3 world = glm::vec3(found->second * glm::vec4(local, 1.0f));
					low = glm::min(low, world);
					high = glm::max(high, world);
				}
				add(low, high);
			});
		}
		m_region_generation = generation;
	}
	m_dirty_known = m_dirty_known && known;
}

auto GiPass::writeDirty(uint32_t frame_index) -> bool {
	m_dirty_stats[frame_index] = std::exchange(m_dirty_pending, Stats {});
	const bool known = std::exchange(m_dirty_known, true);
	const auto count = static_cast<uint32_t>(m_dirty_scratch.size() / 2);
	const auto& allocation = m_dirty[frame_index].getAllocation();
	auto* mapped = static_cast<glm::vec4*>(allocation.getInfo().pMappedData);
	mapped[0] = glm::vec4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);
	for (uint32_t i = 0; i < count * 2; ++i) {
		mapped[1 + i] = glm::vec4(m_dirty_scratch[i], 0.0f);
	}
	allocation.flush(0, sizeof(glm::vec4) * (1 + (count * 2)));
	m_dirty_scratch.clear();
	return known;
}

auto GiPass::sceneBuffer(std::string_view name) const -> vk::Buffer {
	const auto handle = [](const std::optional<vma::raii::Buffer>& buffer) {
		return buffer.has_value() ? **buffer : vk::Buffer {};
	};
	if (name == "gGiFrame") {
		return handle(m_published);
	}
	if (name == "gGiTableKeys") {
		return handle(m_table_keys);
	}
	if (name == "gGiTableValues") {
		return handle(m_table_values);
	}
	if (name == "gGiPool") {
		return handle(m_pool);
	}
	return {};
}

auto GiPass::stats() const -> Stats {
	std::scoped_lock lock(m_stats_mutex);
	return m_stats;
}

void GiPass::readStats(uint32_t frame_index) {
	if (!m_readback_valid[frame_index]) {
		return;
	}
	m_readback_valid[frame_index] = false;
	const auto& allocation = m_readback[frame_index].getAllocation();
	allocation.invalidate(0, sizeof(uint32_t) * k_counter_words);
	std::array<uint32_t, k_counter_words> words {};
	std::memcpy(words.data(), allocation.getInfo().pMappedData, sizeof(uint32_t) * k_counter_words);

	const uint32_t parity = m_readback_parity[frame_index];
	Stats stats;
	stats.cascades = m_quality.cascades;
	for (uint32_t i = 0; i < k_max_cascades; ++i) {
		stats.probes[i] = std::min(words[(parity * 8) + i], m_cascades[i].capacity);
		stats.seen[i] = words[k_counter_seen + i];
		stats.capacity[i] = m_cascades[i].capacity;
		stats.deposits[i] = std::min(words[k_counter_seen + i], m_cascades[i].deposit_capacity);
		stats.deposit_capacity[i] = m_cascades[i].deposit_capacity;
		const uint64_t sampled = ((static_cast<uint64_t>(stats.deposits[i]) * m_cascades[i].directions) + 63) / 64;
		stats.depth[i] = sampled > 0 ? static_cast<float>(words[k_counter_depth + i]) / (16.0f * static_cast<float>(sampled)) : 0.0f;
	}
	stats.table_full = words[k_counter_table_full];
	stats.pool_full = words[k_counter_pool_full];
	stats.deposit_full = words[k_counter_deposit_full];
	stats.rays = words[k_counter_rays];
	stats.slots = std::min(words[k_counter_slots], k_deposit_slots);
	stats.moved = m_dirty_stats[frame_index].moved;
	stats.appeared = m_dirty_stats[frame_index].appeared;
	stats.edits = m_dirty_stats[frame_index].edits;
	stats.largest_motion = m_dirty_stats[frame_index].largest_motion;
	stats.invalidated = words[k_counter_invalidated];
	stats.fresh = words[k_counter_fresh];
	stats.blend = m_blend;
	const uint32_t budget = m_readback_point_budget[frame_index];
	stats.points = std::min(words[k_counter_points + parity], budget);
	stats.points_dropped = words[k_counter_points_dropped];
	if (words[k_counter_spawn] > m_readback_spawn_capacity[frame_index] && !m_spawn_overflow_logged) {
		m_spawn_overflow_logged = true;
		TOAST_WARN(
		    "Render",
		    "GI spawn pixels ({}) exceed the spawn list ({}) so the extra pixels trace no rays this frame",
		    words[k_counter_spawn],
		    m_readback_spawn_capacity[frame_index]
		);
	}

	// Hits offered at a known share give the whole demand so the share fills the budget to 90% without a race
	const float offered = static_cast<float>(words[k_counter_points + parity]);
	const float demand = offered / std::max(m_readback_point_share[frame_index], 1e-3f);
	m_point_share = demand > 0.0f ? std::clamp(0.9f * static_cast<float>(budget) / demand, 1.0f / 64.0f, 1.0f) : 1.0f;
	stats.point_share = m_point_share;
	stats.quality = m_quality.level;
	stats.ray_step = m_quality.step;
	stats.update_interval = m_quality.interval;
	stats.point_budget = m_quality.multibounce ? m_quality.points : 0;

	std::scoped_lock lock(m_stats_mutex);
	m_stats = stats;
}

void GiPass::bindProbeSet(uint32_t frame_index) {
	const vk::ImageView depth_view = VulkanRenderer::instance->getGiDepthView();
	const vk::ImageView normal_view = VulkanRenderer::instance->getSceneNormalView();
	const std::array wanted {depth_view, normal_view};
	if (m_probe_bound[frame_index] == wanted) {
		return;
	}
	DescriptorWriter writer;
	writer.image(
	    *m_probe_sets[frame_index],
	    k_gi_depth_binding,
	    vk::DescriptorType::eCombinedImageSampler,
	    *m_point_sampler,
	    depth_view,
	    vk::ImageLayout::eDepthReadOnlyOptimal
	);
	writer.image(
	    *m_probe_sets[frame_index],
	    k_normal_binding,
	    vk::DescriptorType::eCombinedImageSampler,
	    *m_point_sampler,
	    normal_view,
	    vk::ImageLayout::eShaderReadOnlyOptimal
	);
	writer.flush(m_core->getDevice());
	m_probe_bound[frame_index] = wanted;
}

auto GiPass::bindRaySets(uint32_t frame_index, vk::ImageView scene_view) -> bool {
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (!m_ray_pipeline.isReady() || !scene_view || VulkanRenderer::instance->voxelTraceScene() == nullptr ||
	    !frame->voxel_storage || !frame->voxel_storage->isReady()) {
		return false;
	}
	// Safe since the fence of this slot was waited on
	if (m_ray_storage_bound[frame_index] != frame->voxel_storage) {
		writeVoxelStorageDescriptors(m_core->getDevice(), *m_ray_storage_sets[frame_index], *frame->voxel_storage);
		m_ray_storage_bound[frame_index] = frame->voxel_storage;
	}

	const vk::ImageView depth_view = VulkanRenderer::instance->getGiDepthView();
	const vk::ImageView normal_view = VulkanRenderer::instance->getSceneNormalView();
	const std::array wanted {depth_view, normal_view, scene_view};
	if (m_ray_bound[frame_index] != wanted) {
		DescriptorWriter writer;
		writer.image(
		    *m_ray_sets[frame_index],
		    k_ray_scene_colour_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_point_sampler,
		    scene_view,
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.image(
		    *m_ray_sets[frame_index],
		    k_ray_depth_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_point_sampler,
		    depth_view,
		    vk::ImageLayout::eDepthReadOnlyOptimal
		);
		writer.image(
		    *m_ray_sets[frame_index],
		    k_ray_normal_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_point_sampler,
		    normal_view,
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.flush(m_core->getDevice());
		m_ray_bound[frame_index] = wanted;
	}
	m_ray_scene_sets.updateTlas(frame_index);
	return true;
}

auto GiPass::recordTrace(
    vk::CommandBuffer cmd, uint32_t frame_index, uint32_t spawn_x, uint32_t spawn_y, vk::ImageView scene_view
) -> bool {
	ZoneScoped;
	// Hits without a shade pass would queue and never deposit
	if (!m_shade_pipeline.isReady() || !bindRaySets(frame_index, scene_view)) {
		return false;
	}
	const vk::PipelineLayout layout = *m_ray_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_ray_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute,
	    layout,
	    0,
	    std::array {m_ray_scene_sets.get(frame_index), *m_ray_storage_sets[frame_index], *m_ray_sets[frame_index]},
	    {}
	);
	cmd.dispatch(groups(std::min(spawn_x * spawn_y, m_spawn_capacity), k_group), 1, 1);
	if (m_point_trace_pipeline.isReady()) {
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_point_trace_pipeline.getPipeline());
		cmd.dispatch(groups(m_quality.points, k_group), 1, 1);
	}
	return true;
}

void GiPass::recordShade(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t rays) {
	ZoneScoped;
	if (!m_shade_pipeline.isReady()) {
		return;
	}
	const vk::PipelineLayout layout = *m_ray_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_shade_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute,
	    layout,
	    0,
	    std::array {m_ray_scene_sets.get(frame_index), *m_ray_storage_sets[frame_index], *m_ray_sets[frame_index]},
	    {}
	);
	cmd.dispatch(groups(rays, k_group), 1, 1);
}

void GiPass::recordMerge(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t cascades) {
	ZoneScoped;
	const vk::PipelineLayout layout = *m_merge_layout.getPipelineLayout();
	m_merge_scene_sets.updateTlas(frame_index);
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute, layout, 0, std::array {m_merge_scene_sets.get(frame_index), *m_merge_sets[frame_index]}, {}
	);

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_merge_pipeline.getPipeline());
	for (uint32_t i = cascades; i-- > 1;) {
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(uint32_t), &i);
		cmd.dispatch(k_stride_groups, 1, 1);
		computeBarrier(cmd);
	}

	sync::BarrierBatch barriers;
	barriers.image(m_atlas_state, sync::Usage::compute_storage_write);
	barriers.flush(cmd);
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_irradiance_pipeline.getPipeline());
	const uint32_t c0 = 0;
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(uint32_t), &c0);
	// Mirrors gi_merge irradianceMain with two tiles per group
	cmd.dispatch(k_atlas_tiles_per_row / 2, groups(m_cascades[0].deposit_capacity, k_atlas_tiles_per_row), 1);
	barriers.image(m_atlas_state, sync::Usage::shader_sampled);
	barriers.flush(cmd);
}

auto GiPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	const auto* renderer = VulkanRenderer::instance;
	const auto* frame = renderer->renderingFrame();
	const GiSettings& settings = renderer->giSettings();
	const bool enabled = settings.enabled.load(std::memory_order_relaxed);
	// Lighting falls back to the flat ambient once the frame it reads says no GI
	if (!enabled && m_published_valid) {
		sync::BarrierBatch barriers;
		barriers.memory(sync::Usage::fragment_storage_read, sync::Usage::transfer_dst);
		barriers.memory(sync::Usage::compute_storage_read, sync::Usage::transfer_dst);
		barriers.flush(cmd);
		cmd.fillBuffer(**m_published, 0, vk::WholeSize, 0u);
		barriers.memory(sync::Usage::transfer_dst, sync::Usage::fragment_storage_read);
		barriers.memory(sync::Usage::transfer_dst, sync::Usage::compute_storage_read);
		barriers.flush(cmd);
		m_published_valid = false;
	}
	if (frame == nullptr || frame_index >= m_probe_sets.size() || !enabled || !renderer->getDepthView() ||
	    !renderer->getSceneNormalView()) {
		return source_view;
	}

	// The fence of this slot was waited on so its last readback is complete
	readStats(frame_index);
	gatherDirty();

	m_quality = resolveQuality();
	const Quality& quality = m_quality;
	if (quality.level == GiSettings::k_quality_custom &&
	    quality.step > std::clamp(settings.ray_step.load(std::memory_order_relaxed), 1u, 4u) && !m_spawn_step_logged) {
		m_spawn_step_logged = true;
		TOAST_INFO("Render", "GI custom ray step raised to {} to fit the spawn list", quality.step);
	}
	const uint32_t cascades = quality.cascades;
	const uint32_t step = quality.step;
	const float spacing = std::max(quality.spacing, 0.01f);
	const KeySettings keys {
	  .spacing = spacing,
	  .lod_distance = std::clamp(quality.lod_distance, 0.25f, spacing * k_max_cells_to_lod),
	};
	const bool carry = m_key_settings.has_value() && *m_key_settings == keys;
	const uint32_t debug_view = settings.debug_view.load(std::memory_order_relaxed);
	const uint32_t debug_mode = std::min(settings.debug_mode.load(std::memory_order_relaxed), 5u);

	// Lighting keeps reading the last published tables on skipped frames
	if (carry && m_published_valid && debug_view == 0 && ++m_skipped < quality.interval) {
		TracyPlot("GI: updated", static_cast<int64_t>(0));
		return source_view;
	}
	TracyPlot("GI: updated", static_cast<int64_t>(1));
	TracyPlot("GI: update interval", static_cast<int64_t>(quality.interval));
	m_skipped = 0;
	m_key_settings = keys;

	++m_frame;
	const uint32_t parity = m_frame & 1u;
	const bool history_known = writeDirty(frame_index);
	const auto now = std::chrono::steady_clock::now();
	const float frame_seconds = m_last_record == std::chrono::steady_clock::time_point {}
	                                ? 1.0f / 60.0f
	                                : std::clamp(std::chrono::duration<float>(now - m_last_record).count(), 0.001f, 0.1f);
	m_last_record = now;
	const float history_seconds = settings.history_seconds.load(std::memory_order_relaxed);
	float history = 1.0f;
	if (settings.history_per_frame.load(std::memory_order_relaxed)) {
		history = std::clamp(settings.history_blend.load(std::memory_order_relaxed), 0.005f, 1.0f);
	} else if (history_seconds > 0.0f) {
		history = std::clamp(1.0f - std::exp(-frame_seconds / history_seconds), 0.001f, 1.0f);
	}
	m_blend = history;
	// Narrowest bucket of the top cascade so the march never coarsens past what any interval resolves
	const float cone_slope = std::sqrt(4.0f * glm::pi<float>() / static_cast<float>(m_cascades[cascades - 1].directions));
	FrameData data {
	  .inverse_view_projection = glm::inverse(frame->frame_data.jittered_view_projection),
	  .camera = glm::vec4(frame->frame_data.camera_position, keys.spacing),
	  .lod = glm::vec4(
	      keys.lod_distance,
	      k_lod_overlap,
	      static_cast<float>(settings.probe_lifetime.load(std::memory_order_relaxed)),
	      static_cast<float>(step)
	  ),
	  .frame = glm::uvec4(m_frame, cascades, parity, carry ? 1u : 0u),
	  // The spawn pixel walks every position of its block so each pixel inserts once every step squared frames
	  .viewport = glm::uvec4(m_extent.width, m_extent.height, m_frame % step, (m_frame / step) % step),
	  .pool = glm::uvec4(m_pool_total, m_table_total, m_deposit_total, k_deposit_slots),
	  .rays = glm::vec4(k_interval_scale, static_cast<float>(renderer->voxelSunReach()), k_deposit_fixed_scale, cone_slope),
	  .jitter = glm::vec4(r2Jitter(m_frame), history_known ? 1.0f - history : 0.0f, k_reset_history),
	  .secondary = glm::uvec4(k_max_points, quality.multibounce ? 1u : 0u, std::bit_cast<uint32_t>(m_point_share), quality.points),
	  // Rare directions hold 2 / blend rays fading eight times slower than the blend so they average as many as busy ones
	  .limits = glm::vec4(
	      std::max(settings.max_ray_radiance.load(std::memory_order_relaxed), 0.01f),
	      history >= 1.0f ? 0.0f : 2.0f / history,
	      history_known ? 1.0f - (history / 8.0f) : 0.0f,
	      0.0f
	  ),
	};
	for (uint32_t i = 0; i < k_max_cascades; ++i) {
		const Cascade& c = m_cascades[i];
		data.cascades[i] = glm::uvec4(c.table_offset, c.table_size - 1, c.pool_offset, c.capacity);
		data.deposits[i] = glm::uvec4(c.deposit_offset, c.deposit_capacity, 0u, c.directions);
	}
	data.queues = glm::uvec4(m_spawn_capacity, m_spawn_capacity, m_spawn_capacity + k_max_points, 0u);
	const auto& frame_allocation = m_frame_data[frame_index].getAllocation();
	std::memcpy(frame_allocation.getInfo().pMappedData, &data, sizeof(FrameData));
	frame_allocation.flush(0, sizeof(FrameData));

	bindProbeSet(frame_index);

	// Lighting read the tables and pool of the last merge earlier in this frame
	sync::BarrierBatch war;
	war.memory(sync::Usage::fragment_storage_read, sync::Usage::compute_storage_write);
	war.memory(sync::Usage::compute_storage_read, sync::Usage::compute_storage_write);
	war.flush(cmd);

	const vk::PipelineLayout layout = *m_probe_layout.getPipelineLayout();
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eCompute, layout, 0, std::array<vk::DescriptorSet, 1> {*m_probe_sets[frame_index]}, {}
	);
	const auto dispatch = [&](const VulkanPipeline& pipeline, uint32_t cascade, uint32_t x, uint32_t y = 1, uint32_t carried = 0) {
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.getPipeline());
		const std::array push {cascade, carried};
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), push.data());
		cmd.dispatch(x, y, 1);
	};

	GpuTimer* timer = VulkanRenderer::instance->gpuTimer();
	const uint32_t spawn_x = (m_extent.width + step - 1) / step;
	const uint32_t spawn_y = (m_extent.height + step - 1) / step;
	{
		const GpuScope scope(timer, frame_index, cmd, "GI probes");
		dispatch(m_clear_pipeline, 0, groups(m_table_total, k_group));
		computeBarrier(cmd);
		for (uint32_t i = 0; i < cascades; ++i) {
			dispatch(m_carry_pipeline, i, groups(m_cascades[i].capacity, k_group));
		}
		computeBarrier(cmd);
		dispatch(m_pixel_pipeline, 0, groups(spawn_x, k_pixel_group), groups(spawn_y, k_pixel_group));
		dispatch(m_point_pipeline, 0, groups(quality.points, k_group));
		computeBarrier(cmd);
		for (uint32_t i = 0; i + 1 < cascades; ++i) {
			dispatch(m_parent_pipeline, i, groups(m_cascades[i].table_size, k_group));
			computeBarrier(cmd);
		}
		for (uint32_t i = 0; i < cascades; ++i) {
			dispatch(m_compact_pipeline, i, groups(m_cascades[i].table_size, k_group));
		}
		computeBarrier(cmd);
		for (uint32_t i = 0; i < cascades; ++i) {
			dispatch(m_keep_pipeline, i, groups(m_cascades[i].capacity, k_group));
		}
		computeBarrier(cmd);
	}
	{
		const GpuScope scope(timer, frame_index, cmd, "GI deposits");
		for (uint32_t carried = 0; carried < 2; ++carried) {
			for (uint32_t i = cascades; i-- > 0;) {
				dispatch(m_slots_pipeline, i, groups(m_cascades[i].deposit_capacity, k_group), 1, carried);
				computeBarrier(cmd);
			}
		}
		dispatch(m_deposit_clear_pipeline, 0, k_stride_groups);
		for (uint32_t i = 0; i + 1 < cascades; ++i) {
			dispatch(m_link_pipeline, i, groups(m_cascades[i].deposit_capacity, k_group));
		}
		dispatch(m_count_pipeline, 0, groups(spawn_x, k_pixel_group), groups(spawn_y, k_pixel_group));
		dispatch(m_point_count_pipeline, 0, groups(quality.points, k_group));
		computeBarrier(cmd);
		for (uint32_t i = 0; i + 1 < cascades; ++i) {
			dispatch(m_propagate_pipeline, i, groups(m_cascades[i].deposit_capacity, k_group));
			computeBarrier(cmd);
		}
		for (uint32_t i = cascades; i-- > 0;) {
			dispatch(m_offset_pipeline, i, groups(m_cascades[i].deposit_capacity, k_group));
			computeBarrier(cmd);
		}
	}
	bool traced = false;
	{
		const GpuScope scope(timer, frame_index, cmd, "GI trace");
		traced = recordTrace(cmd, frame_index, spawn_x, spawn_y, source_view);
		// Runs without a trace too so a scene with no voxels still sees the sky
		computeBarrier(cmd);
	}
	if (traced) {
		const GpuScope scope(timer, frame_index, cmd, "GI shade");
		recordShade(cmd, frame_index, std::min(spawn_x * spawn_y, m_spawn_capacity) + m_quality.points);
		computeBarrier(cmd);
	}
	{
		const GpuScope scope(timer, frame_index, cmd, "GI merge");
		recordMerge(cmd, frame_index, cascades);
	}
	if (debug_view > 0 && debug_mode == 5) {
		recordChange(cmd, frame_index, source_view);
	}

	sync::BarrierBatch barriers;
	// The copy of the frame before wrote the published frame too
	barriers.memory(sync::Usage::transfer_dst, sync::Usage::transfer_dst);
	barriers.memory(sync::Usage::compute_storage_write, sync::Usage::transfer_src);
	barriers.memory(sync::Usage::compute_storage_write, sync::Usage::fragment_storage_read);
	barriers.memory(sync::Usage::compute_storage_write, sync::Usage::compute_storage_read);
	barriers.flush(cmd);
	cmd.copyBuffer(**m_counters, *m_readback[frame_index], vk::BufferCopy(0, 0, sizeof(uint32_t) * k_counter_words));
	cmd.copyBuffer(*m_frame_data[frame_index], **m_published, vk::BufferCopy(0, 0, sizeof(FrameData)));
	m_published_valid = true;
	barriers.memory(sync::Usage::transfer_dst, sync::Usage::host_read);
	barriers.memory(sync::Usage::transfer_dst, sync::Usage::fragment_storage_read);
	barriers.memory(sync::Usage::transfer_dst, sync::Usage::compute_storage_read);
	barriers.flush(cmd);
	m_readback_valid[frame_index] = true;
	m_readback_parity[frame_index] = parity;
	m_readback_point_share[frame_index] = m_point_share;
	m_readback_point_budget[frame_index] = quality.points;
	m_readback_spawn_capacity[frame_index] = m_spawn_capacity;

	if (debug_view == 0 || !source_view || !m_debug_pipeline.isReady() || !m_debug_target.isReady()) {
		return source_view;
	}
	return recordDebug(cmd, frame_index, source_view, debug_view - 1, debug_mode);
}

auto GiPass::recordDebug(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view, uint32_t cascade, uint32_t mode)
    -> vk::ImageView {
	ZoneScoped;
	const vk::ImageView depth_view = VulkanRenderer::instance->getGiDepthView();
	const vk::ImageView normal_view = VulkanRenderer::instance->getSceneNormalView();
	const std::array wanted {source_view, depth_view, normal_view};
	if (m_debug_bound[frame_index] != wanted) {
		DescriptorWriter writer;
		writer.image(
		    *m_debug_sets[frame_index],
		    k_debug_source_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_point_sampler,
		    source_view,
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.image(
		    *m_debug_sets[frame_index],
		    k_debug_depth_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_point_sampler,
		    depth_view,
		    vk::ImageLayout::eDepthReadOnlyOptimal
		);
		writer.image(
		    *m_debug_sets[frame_index],
		    k_debug_normal_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_point_sampler,
		    normal_view,
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.flush(m_core->getDevice());
		m_debug_bound[frame_index] = wanted;
	}

	const vk::PipelineLayout layout = *m_debug_layout.getPipelineLayout();
	m_debug_target.beginScope(cmd);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics, layout, 0, std::array<vk::DescriptorSet, 1> {*m_debug_sets[frame_index]}, {}
	);
	const std::array push {cascade, mode};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), push.data());
	cmd.draw(3, 1, 0, 0);
	m_debug_target.endScope(cmd);
	return m_debug_target.view();
}

}
