/// @file gi_clipmap_pass.cpp
/// @author dario
/// @date 10/11/2026

#include "toast/renderer/passes/lighting/gi_clipmap_pass.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/core/gpu_timer.hpp"
#include "toast/renderer/lighting/gi_clip_math.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/voxel/voxel_gpu_storage.hpp"
#include "toast/renderer/voxel/voxel_trace_scene.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <limits>

namespace renderer {

namespace {

constexpr uint32_t k_fill_set = 0;
constexpr uint32_t k_storage_set = 1;
constexpr uint32_t k_fill_frame_binding = 0;
constexpr uint32_t k_fill_material_binding = 1;
constexpr uint32_t k_fill_emission_binding = 2;
constexpr uint32_t k_fill_opacity_binding = 3;
constexpr uint32_t k_fill_ambient_binding = 4;
/// Mirrors scene_trace.slang
constexpr uint32_t k_trace_volumes_binding = 10;
constexpr uint32_t k_trace_bounds_binding = 11;
constexpr uint32_t k_trace_grid_binding = 12;

constexpr uint32_t k_debug_source_binding = 0;
constexpr uint32_t k_debug_frame_binding = 1;
constexpr uint32_t k_debug_material_binding = 2;
constexpr uint32_t k_debug_emission_binding = 3;
constexpr uint32_t k_debug_radiance_binding = 4;

constexpr uint32_t k_inject_scene_set = 0;
constexpr uint32_t k_inject_set = 1;
constexpr uint32_t k_inject_frame_binding = 0;
constexpr uint32_t k_inject_material_binding = 1;
constexpr uint32_t k_inject_emission_binding = 2;
constexpr uint32_t k_inject_radiance_binding = 3;
constexpr uint32_t k_inject_ambient_binding = 4;
constexpr uint32_t k_inject_opacity_binding = 5;

constexpr uint32_t k_down_frame_binding = 0;
constexpr uint32_t k_down_child_opacity_binding = 1;
constexpr uint32_t k_down_child_radiance_binding = 2;
constexpr uint32_t k_down_child_ambient_binding = 3;
constexpr uint32_t k_down_opacity_binding = 4;
constexpr uint32_t k_down_radiance_binding = 5;
constexpr uint32_t k_down_ambient_binding = 6;
constexpr size_t k_max_down_boxes = 16;

constexpr uint32_t k_bounce_scene_set = 0;
constexpr uint32_t k_bounce_set = 1;
constexpr uint32_t k_bounce_ambient_binding = 0;
constexpr uint32_t k_bounce_list_binding = 1;
/// Mirrors gi_clip_bounce kFaceListCapacity and kFaceListHeader
constexpr uint32_t k_bounce_capacity = 1u << 17;
constexpr uint32_t k_bounce_header = 4;
/// Mirrors gi_clip_bounce bounceMain numthreads
constexpr uint32_t k_bounce_group = 64;
constexpr uint32_t k_inject_period = 8;

/// Mirrors gi_clip_fill numthreads
constexpr int k_group = 4;
constexpr vk::Format k_material_format = vk::Format::eR8G8B8A8Unorm;
constexpr vk::Format k_emission_format = vk::Format::eR16G16B16A16Sfloat;
/// Same texel size so one image takes both views and only mandatory format features are used
constexpr vk::Format k_radiance_storage_format = vk::Format::eR32Uint;
constexpr vk::Format k_radiance_sampled_format = vk::Format::eB10G11R11UfloatPack32;

/// Mirrors gi_clip_down GiClipDownPush
struct DownPush {
	/// xyz first global cell written w cells a side of the texture written
	glm::ivec4 box_min {0};
	/// xyz cells written w cells a side of the texture read
	glm::ivec4 box_size {0};
	/// x mip of the texture read
	glm::ivec4 mip {0};
};

/// Mips above the last level down to 4 cells a side which the toroidal addressing needs to divide evenly
auto tailMips(uint32_t resolution) -> uint32_t {
	uint32_t count = 0;
	while (count < GiClipmapPass::k_max_tail && (resolution >> (count + 1)) >= 4 && resolution % (1u << (count + 1)) == 0) {
		++count;
	}
	return count;
}

/// Mirrors gi_clip_fill GiClipFillPush
struct FillPush {
	/// xyz first global cell w level
	glm::ivec4 box_min {0};
	glm::ivec4 box_size {0};
};

}

GiClipmapPass::GiClipmapPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent)
    : m_core(&core),
      m_scene_format(scene_format),
      m_extent(extent) {
	ZoneScoped;
	static_assert(sizeof(FrameData) == 192, "FrameData is mirrored by gi_clip_common GiClipFrame");

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(FrameData);
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAuto;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
		m_frame_data.push_back(core.getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(core, *m_frame_data.back(), std::format("GiClipmapPass Frame[{}]", i));
	}
	m_point_sampler = vk::raii::Sampler(core.getDevice(), nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "GiClipmapPass PointSampler");
	auto linear_ci = linearClampSamplerInfo();
	linear_ci.addressModeU = vk::SamplerAddressMode::eRepeat;
	linear_ci.addressModeV = vk::SamplerAddressMode::eRepeat;
	linear_ci.addressModeW = vk::SamplerAddressMode::eRepeat;
	linear_ci.mipmapMode = vk::SamplerMipmapMode::eLinear;
	linear_ci.maxLod = vk::LodClampNone;
	m_linear_sampler = vk::raii::Sampler(core.getDevice(), linear_ci);
	setDebugName(core, *m_linear_sampler, "GiClipmapPass LinearSampler");

	vk::BufferCreateInfo published_ci {};
	published_ci.size = sizeof(FrameData);
	published_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
	vma::AllocationCreateInfo published_alloc {};
	published_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_published.emplace(core.getAllocator().createBuffer(published_ci, published_alloc));
	setDebugName(core, **m_published, "GiClipmapPass PublishedFrame");

	vk::ImageCreateInfo dummy_ci {};
	dummy_ci.imageType = vk::ImageType::e3D;
	dummy_ci.format = k_material_format;
	dummy_ci.extent = vk::Extent3D {1, 1, 1};
	dummy_ci.mipLevels = 1;
	dummy_ci.arrayLayers = 1;
	dummy_ci.samples = vk::SampleCountFlagBits::e1;
	dummy_ci.tiling = vk::ImageTiling::eOptimal;
	dummy_ci.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	dummy_ci.sharingMode = vk::SharingMode::eExclusive;
	dummy_ci.initialLayout = vk::ImageLayout::eUndefined;
	vma::AllocationCreateInfo dummy_alloc {};
	dummy_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_dummy.emplace(core.getAllocator().createImage(dummy_ci, dummy_alloc));
	setDebugName(core, **m_dummy, "GiClipmapPass Dummy");
	const vk::ImageSubresourceRange dummy_range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	vk::ImageViewCreateInfo dummy_view_ci {};
	dummy_view_ci.image = **m_dummy;
	dummy_view_ci.viewType = vk::ImageViewType::e3D;
	dummy_view_ci.format = k_material_format;
	dummy_view_ci.subresourceRange = dummy_range;
	m_dummy_view.emplace(core.getDevice(), dummy_view_ci);

	// Lighting binds both before the first fill so they start readable and empty
	const auto& device = core.getDevice();
	const vk::raii::CommandPool one_shot_pool(
	    device, vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, core.getGraphicsQueueFamilyIndex())
	);
	auto cmd_buffers =
	    device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(*one_shot_pool, vk::CommandBufferLevel::ePrimary, 1));
	const vk::raii::CommandBuffer cmd = std::move(cmd_buffers[0]);
	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	cmd.fillBuffer(**m_published, 0, vk::WholeSize, 0u);
	recordUndefinedToTransferDst(*cmd, **m_dummy, dummy_range);
	cmd.clearColorImage(**m_dummy, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f), dummy_range);
	recordTransferDstToShaderRead(*cmd, **m_dummy, dummy_range);
	sync::BarrierBatch barriers;
	barriers.memory(sync::Usage::transfer_dst, sync::Usage::general);
	barriers.flush(*cmd);
	cmd.end();
	submitAndWait(device, core.getGraphicsQueue(), core.graphicsSubmitMutex(), *cmd);

	if (!createPipelines(core)) {
		setEnabled(false);
		return;
	}
	m_debug_target.create(core, extent, scene_format, "GiClipmapPass Debug");
}

auto GiClipmapPass::createPipelines(const VulkanCore& core) -> bool {
	ZoneScoped;
	const auto fill_uid = assets::resolveURI("core://shaders/gi_clip_fill.slang");
	const auto fill_shader = fill_uid.has_value() ? ShaderCache::get().acquire(*fill_uid) : nullptr;
	const auto debug_uid = assets::resolveURI("core://shaders/gi_clip_debug.slang");
	const auto debug_shader = debug_uid.has_value() ? ShaderCache::get().acquire(*debug_uid) : nullptr;
	const auto inject_uid = assets::resolveURI("core://shaders/gi_clip_inject.slang");
	const auto inject_shader = inject_uid.has_value() ? ShaderCache::get().acquire(*inject_uid) : nullptr;
	const auto bounce_uid = assets::resolveURI("core://shaders/gi_clip_bounce.slang");
	const auto bounce_shader = bounce_uid.has_value() ? ShaderCache::get().acquire(*bounce_uid) : nullptr;
	const auto down_uid = assets::resolveURI("core://shaders/gi_clip_down.slang");
	const auto down_shader = down_uid.has_value() ? ShaderCache::get().acquire(*down_uid) : nullptr;
	if (!fill_shader || !debug_shader || !inject_shader || !bounce_shader || !down_shader) {
		TOAST_ERROR("Render", "GiClipmapPass shaders unavailable, the radiance clipmap is disabled");
		return false;
	}

	m_fill_layout.rebuild(core, fill_shader->reflection, "GiClipmapPass Fill");
	VulkanPipeline::Config fill_config;
	fill_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	fill_config.debug_name = "GiClipmapPass fillMain";
	fill_config.shader_spirv = fill_shader->spirv;
	fill_config.pipeline_layout = *m_fill_layout.getPipelineLayout();
	fill_config.compute_entry = "fillMain";
	m_fill_pipeline.rebuild(core, fill_config);

	m_bounce_layout.rebuild(core, bounce_shader->reflection, "GiClipmapPass Bounce");
	VulkanPipeline::Config bounce_config;
	bounce_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	bounce_config.debug_name = "GiClipmapPass bounceMain";
	bounce_config.shader_spirv = bounce_shader->spirv;
	bounce_config.pipeline_layout = *m_bounce_layout.getPipelineLayout();
	bounce_config.compute_entry = "bounceMain";
	m_bounce_pipeline.rebuild(core, bounce_config);
	bounce_config.debug_name = "GiClipmapPass listMain";
	bounce_config.compute_entry = "listMain";
	m_bounce_list_pipeline.rebuild(core, bounce_config);

	vk::BufferCreateInfo list_ci {};
	list_ci.size = sizeof(uint32_t) * (k_bounce_header + (4 * k_bounce_capacity));
	list_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
	vma::AllocationCreateInfo list_alloc {};
	list_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_bounce_list.emplace(core.getAllocator().createBuffer(list_ci, list_alloc));
	setDebugName(core, **m_bounce_list, "GiClipmapPass BounceList");

	m_down_layout.rebuild(core, down_shader->reflection, "GiClipmapPass Down");
	VulkanPipeline::Config down_config;
	down_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	down_config.debug_name = "GiClipmapPass downMain";
	down_config.shader_spirv = down_shader->spirv;
	down_config.pipeline_layout = *m_down_layout.getPipelineLayout();
	down_config.compute_entry = "downMain";
	m_down_pipeline.rebuild(core, down_config);
	down_config.debug_name = "GiClipmapPass mipMain";
	down_config.compute_entry = "mipMain";
	m_mip_pipeline.rebuild(core, down_config);

	m_inject_layout.rebuild(core, inject_shader->reflection, "GiClipmapPass Inject");
	VulkanPipeline::Config inject_config;
	inject_config.pipeline_type = VulkanPipeline::PipelineType::compute;
	inject_config.debug_name = "GiClipmapPass injectMain";
	inject_config.shader_spirv = inject_shader->spirv;
	inject_config.pipeline_layout = *m_inject_layout.getPipelineLayout();
	inject_config.compute_entry = "injectMain";
	m_inject_pipeline.rebuild(core, inject_config);

	m_debug_layout.rebuild(core, debug_shader->reflection, "GiClipmapPass Debug");
	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "GiClipmapPass Debug";
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

	const auto& fill_layouts = m_fill_layout.getDescriptorSetLayouts();
	const auto& debug_layouts = m_debug_layout.getDescriptorSetLayouts();
	const auto& inject_layouts = m_inject_layout.getDescriptorSetLayouts();
	const auto& bounce_layouts = m_bounce_layout.getDescriptorSetLayouts();
	const auto& down_layouts = m_down_layout.getDescriptorSetLayouts();
	if (fill_layouts.size() <= k_storage_set || debug_layouts.empty() || inject_layouts.size() <= k_inject_set ||
	    bounce_layouts.size() <= k_bounce_set || down_layouts.empty()) {
		TOAST_ERROR("Render", "GiClipmapPass shader layouts have no descriptor sets, the radiance clipmap is disabled");
		return false;
	}

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout fill_set_layout = *fill_layouts[k_fill_set];
	const vk::DescriptorSetLayout storage_set_layout = *fill_layouts[k_storage_set];
	const vk::DescriptorSetLayout debug_set_layout = *debug_layouts[0];
	const vk::DescriptorSetLayout inject_set_layout = *inject_layouts[k_inject_set];
	m_inject_scene_sets.create(core, inject_shader->reflection, *inject_layouts[k_inject_scene_set], "GiClipmapPass Inject");
	const vk::DescriptorSetLayout bounce_set_layout = *bounce_layouts[k_bounce_set];
	const vk::DescriptorSetLayout down_set_layout = *down_layouts[0];
	m_bounce_scene_sets.create(core, bounce_shader->reflection, *bounce_layouts[k_bounce_scene_set], "GiClipmapPass Bounce");
	constexpr auto storage = vk::DescriptorType::eStorageBuffer;
	DescriptorWriter writer;
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		for (uint32_t level = 0; level < k_max_levels; ++level) {
			auto fill = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &fill_set_layout));
			m_fill_sets.push_back(std::move(fill[0]));
			setDebugName(core, *m_fill_sets.back(), std::format("GiClipmapPass FillSet[{}][{}]", i, level));
			writer.buffer(*m_fill_sets.back(), k_fill_frame_binding, storage, *m_frame_data[i]);
			auto inject = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &inject_set_layout));
			m_inject_sets.push_back(std::move(inject[0]));
			setDebugName(core, *m_inject_sets.back(), std::format("GiClipmapPass InjectSet[{}][{}]", i, level));
			writer.buffer(*m_inject_sets.back(), k_inject_frame_binding, storage, *m_frame_data[i]);
			auto bounce = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &bounce_set_layout));
			m_bounce_sets.push_back(std::move(bounce[0]));
			setDebugName(core, *m_bounce_sets.back(), std::format("GiClipmapPass BounceSet[{}][{}]", i, level));
			writer.buffer(*m_bounce_sets.back(), k_bounce_list_binding, storage, **m_bounce_list);
			auto down = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &down_set_layout));
			m_down_sets.push_back(std::move(down[0]));
			setDebugName(core, *m_down_sets.back(), std::format("GiClipmapPass DownSet[{}][{}]", i, level));
			writer.buffer(*m_down_sets.back(), k_down_frame_binding, storage, *m_frame_data[i]);
		}
		for (uint32_t mip = 0; mip < k_max_tail; ++mip) {
			auto set = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &down_set_layout));
			m_mip_sets.push_back(std::move(set[0]));
			setDebugName(core, *m_mip_sets.back(), std::format("GiClipmapPass MipSet[{}][{}]", i, mip));
			writer.buffer(*m_mip_sets.back(), k_down_frame_binding, storage, *m_frame_data[i]);
		}
		auto storage_set = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &storage_set_layout));
		m_storage_sets.push_back(std::move(storage_set[0]));
		setDebugName(core, *m_storage_sets.back(), std::format("GiClipmapPass StorageSet[{}]", i));
		auto debug = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &debug_set_layout));
		m_debug_sets.push_back(std::move(debug[0]));
		setDebugName(core, *m_debug_sets.back(), std::format("GiClipmapPass DebugSet[{}]", i));
		writer.buffer(*m_debug_sets.back(), k_debug_frame_binding, storage, *m_frame_data[i]);
	}
	writer.flush(core.getDevice());
	m_storage_bound.resize(VulkanRenderer::k_frames_in_flight);
	m_debug_bound.resize(VulkanRenderer::k_frames_in_flight);
	return m_fill_pipeline.isReady();
}

void GiClipmapPass::createLevels(uint32_t resolution, uint32_t levels) {
	ZoneScoped;
	const VulkanCore& core = *m_core;
	if (m_level_count != 0) {
		const std::lock_guard lock(core.graphicsSubmitMutex());
		core.getDevice().waitIdle();
	}
	m_resolution = resolution;
	m_level_count = levels;
	m_tail = tailMips(resolution);

	const vk::ImageSubresourceRange range {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
	const auto create = [&](vk::Format format,
	                        std::string_view name,
	                        std::optional<vma::raii::Image>& image,
	                        std::optional<vk::raii::ImageView>& view,
	                        sync::ImageState& state,
	                        vk::ImageCreateFlags flags = {},
	                        uint32_t mip_count = 1) {
		vk::ImageCreateInfo image_ci {};
		image_ci.flags = flags;
		image_ci.imageType = vk::ImageType::e3D;
		image_ci.format = format;
		image_ci.extent = vk::Extent3D {resolution, resolution, resolution};
		image_ci.mipLevels = mip_count;
		image_ci.arrayLayers = 1;
		image_ci.samples = vk::SampleCountFlagBits::e1;
		image_ci.tiling = vk::ImageTiling::eOptimal;
		image_ci.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
		image_ci.sharingMode = vk::SharingMode::eExclusive;
		image_ci.initialLayout = vk::ImageLayout::eUndefined;
		vma::AllocationCreateInfo image_alloc {};
		image_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
		view.reset();
		image.emplace(core.getAllocator().createImage(image_ci, image_alloc));
		setDebugName(core, **image, name);
		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **image;
		view_ci.viewType = vk::ImageViewType::e3D;
		view_ci.format = format;
		view_ci.subresourceRange = range;
		view.emplace(core.getDevice(), view_ci);
		state.reset(**image, vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, mip_count, 0, 1});
	};

	DescriptorWriter writer;
	for (uint32_t i = 0; i < k_max_levels; ++i) {
		Level& level = m_levels[i];
		if (i >= levels) {
			level = Level {};
			continue;
		}
		const uint32_t mips = i + 1 == levels ? 1 + m_tail : 1;
		create(
		    k_material_format, std::format("GiClipmapPass Material[{}]", i), level.material, level.material_view, level.material_state
		);
		create(
		    k_emission_format, std::format("GiClipmapPass Emission[{}]", i), level.emission, level.emission_view, level.emission_state
		);
		for (uint32_t f = 0; f < 2 * k_faces; ++f) {
			const bool bounce = f >= k_faces;
			Face& face = bounce ? level.ambient[f - k_faces] : level.faces[f];
			const uint32_t face_mips = bounce ? 1 : mips;
			face.mip_views.clear();
			face.sampled_view.reset();
			create(
			    k_radiance_storage_format,
			    std::format("GiClipmapPass {}[{}][{}]", bounce ? "Ambient" : "Radiance", i, f % k_faces),
			    face.image,
			    face.storage_view,
			    face.state,
			    vk::ImageCreateFlagBits::eMutableFormat,
			    face_mips
			);
			// The packed float format has no storage support so its view only samples
			const vk::ImageViewUsageCreateInfo view_usage(vk::ImageUsageFlagBits::eSampled);
			vk::ImageViewCreateInfo view_ci {};
			view_ci.pNext = &view_usage;
			view_ci.image = **face.image;
			view_ci.viewType = vk::ImageViewType::e3D;
			view_ci.format = k_radiance_sampled_format;
			view_ci.subresourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, face_mips, 0, 1};
			face.sampled_view.emplace(core.getDevice(), view_ci);
			for (uint32_t mip = 1; mip < face_mips; ++mip) {
				vk::ImageViewCreateInfo mip_ci {};
				mip_ci.image = **face.image;
				mip_ci.viewType = vk::ImageViewType::e3D;
				mip_ci.format = k_radiance_storage_format;
				mip_ci.subresourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, mip, 1, 0, 1};
				face.mip_views.emplace_back(core.getDevice(), mip_ci);
			}
		}
		level.opacity_mip_views.clear();
		level.opacity_all_view.reset();
		create(
		    k_material_format,
		    std::format("GiClipmapPass Opacity[{}]", i),
		    level.opacity,
		    level.opacity_view,
		    level.opacity_state,
		    {},
		    mips
		);
		if (mips > 1) {
			vk::ImageViewCreateInfo all_ci {};
			all_ci.image = **level.opacity;
			all_ci.viewType = vk::ImageViewType::e3D;
			all_ci.format = k_material_format;
			all_ci.subresourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, 0, mips, 0, 1};
			level.opacity_all_view.emplace(core.getDevice(), all_ci);
			for (uint32_t mip = 1; mip < mips; ++mip) {
				all_ci.subresourceRange = vk::ImageSubresourceRange {vk::ImageAspectFlagBits::eColor, mip, 1, 0, 1};
				level.opacity_mip_views.emplace_back(core.getDevice(), all_ci);
			}
		}
		level.needs_full = true;
		for (uint32_t frame = 0; frame < VulkanRenderer::k_frames_in_flight; ++frame) {
			const vk::DescriptorSet set = *m_fill_sets[(frame * k_max_levels) + i];
			std::array<vk::DescriptorImageInfo, k_faces> ambient_storage;
			std::array<vk::DescriptorImageInfo, k_faces> ambient_sampled;
			for (uint32_t f = 0; f < k_faces; ++f) {
				ambient_storage[f] = vk::DescriptorImageInfo(nullptr, **level.ambient[f].storage_view, vk::ImageLayout::eGeneral);
				ambient_sampled[f] =
				    vk::DescriptorImageInfo(*m_point_sampler, **level.ambient[f].sampled_view, vk::ImageLayout::eShaderReadOnlyOptimal);
			}
			writer.imageArray(set, k_fill_ambient_binding, vk::DescriptorType::eStorageImage, ambient_storage);
			writer.imageArray(
			    *m_bounce_sets[(frame * k_max_levels) + i], k_bounce_ambient_binding, vk::DescriptorType::eStorageImage, ambient_storage
			);
			writer.imageArray(
			    *m_inject_sets[(frame * k_max_levels) + i],
			    k_inject_ambient_binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    ambient_sampled
			);
			writer.image(
			    set, k_fill_opacity_binding, vk::DescriptorType::eStorageImage, nullptr, **level.opacity_view, vk::ImageLayout::eGeneral
			);
			writer.image(
			    set,
			    k_fill_material_binding,
			    vk::DescriptorType::eStorageImage,
			    nullptr,
			    **level.material_view,
			    vk::ImageLayout::eGeneral
			);
			writer.image(
			    set,
			    k_fill_emission_binding,
			    vk::DescriptorType::eStorageImage,
			    nullptr,
			    **level.emission_view,
			    vk::ImageLayout::eGeneral
			);
		}
	}
	std::array<vk::DescriptorImageInfo, k_max_levels> materials;
	for (uint32_t i = 0; i < k_max_levels; ++i) {
		materials[i] = vk::DescriptorImageInfo(
		    *m_point_sampler, **m_levels[i < levels ? i : 0].material_view, vk::ImageLayout::eShaderReadOnlyOptimal
		);
	}
	for (uint32_t i = 0; i < levels; ++i) {
		const Level& level = m_levels[i];
		for (uint32_t frame = 0; frame < VulkanRenderer::k_frames_in_flight; ++frame) {
			const vk::DescriptorSet set = *m_inject_sets[(frame * k_max_levels) + i];
			writer.imageArray(set, k_inject_material_binding, vk::DescriptorType::eCombinedImageSampler, materials);
			writer.image(
			    set,
			    k_inject_emission_binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    *m_point_sampler,
			    **level.emission_view,
			    vk::ImageLayout::eShaderReadOnlyOptimal
			);
			std::array<vk::DescriptorImageInfo, k_faces> faces;
			for (uint32_t f = 0; f < k_faces; ++f) {
				faces[f] = vk::DescriptorImageInfo(nullptr, **level.faces[f].storage_view, vk::ImageLayout::eGeneral);
			}
			writer.imageArray(set, k_inject_radiance_binding, vk::DescriptorType::eStorageImage, faces);
			writer.image(
			    set,
			    k_inject_opacity_binding,
			    vk::DescriptorType::eCombinedImageSampler,
			    *m_point_sampler,
			    **level.opacity_view,
			    vk::ImageLayout::eShaderReadOnlyOptimal
			);
			if (i == 0) {
				continue;
			}
			const Level& child = m_levels[i - 1];
			const vk::DescriptorSet down = *m_down_sets[(frame * k_max_levels) + i];
			constexpr auto sampled = vk::DescriptorType::eCombinedImageSampler;
			constexpr auto read = vk::ImageLayout::eShaderReadOnlyOptimal;
			std::array<vk::DescriptorImageInfo, k_faces> child_radiance;
			std::array<vk::DescriptorImageInfo, k_faces> child_ambient;
			std::array<vk::DescriptorImageInfo, k_faces> ambient;
			for (uint32_t f = 0; f < k_faces; ++f) {
				child_radiance[f] = vk::DescriptorImageInfo(*m_point_sampler, **child.faces[f].sampled_view, read);
				child_ambient[f] = vk::DescriptorImageInfo(*m_point_sampler, **child.ambient[f].sampled_view, read);
				ambient[f] = vk::DescriptorImageInfo(nullptr, **level.ambient[f].storage_view, vk::ImageLayout::eGeneral);
			}
			writer.image(down, k_down_child_opacity_binding, sampled, *m_point_sampler, **child.opacity_view, read);
			writer.imageArray(down, k_down_child_radiance_binding, sampled, child_radiance);
			writer.imageArray(down, k_down_child_ambient_binding, sampled, child_ambient);
			writer.image(
			    down,
			    k_down_opacity_binding,
			    vk::DescriptorType::eStorageImage,
			    nullptr,
			    **level.opacity_view,
			    vk::ImageLayout::eGeneral
			);
			writer.imageArray(down, k_down_radiance_binding, vk::DescriptorType::eStorageImage, faces);
			writer.imageArray(down, k_down_ambient_binding, vk::DescriptorType::eStorageImage, ambient);
		}
	}
	const Level& top = m_levels[levels - 1];
	for (uint32_t frame = 0; frame < VulkanRenderer::k_frames_in_flight; ++frame) {
		for (uint32_t mip = 1; mip <= m_tail; ++mip) {
			const vk::DescriptorSet set = *m_mip_sets[(frame * k_max_tail) + mip - 1];
			constexpr auto sampled = vk::DescriptorType::eCombinedImageSampler;
			constexpr auto image = vk::DescriptorType::eStorageImage;
			constexpr auto general = vk::ImageLayout::eGeneral;
			std::array<vk::DescriptorImageInfo, k_faces> child_radiance;
			std::array<vk::DescriptorImageInfo, k_faces> child_ambient;
			std::array<vk::DescriptorImageInfo, k_faces> radiance;
			std::array<vk::DescriptorImageInfo, k_faces> ambient;
			for (uint32_t f = 0; f < k_faces; ++f) {
				child_radiance[f] = vk::DescriptorImageInfo(*m_point_sampler, **top.faces[f].sampled_view, general);
				radiance[f] = vk::DescriptorImageInfo(nullptr, *top.faces[f].mip_views[mip - 1], general);
				// mipMain never touches the bounce so these only fill the layout
				child_ambient[f] =
				    vk::DescriptorImageInfo(*m_point_sampler, **top.ambient[f].sampled_view, vk::ImageLayout::eShaderReadOnlyOptimal);
				ambient[f] = vk::DescriptorImageInfo(nullptr, **top.ambient[f].storage_view, general);
			}
			writer.image(set, k_down_child_opacity_binding, sampled, *m_point_sampler, **top.opacity_all_view, general);
			writer.imageArray(set, k_down_child_radiance_binding, sampled, child_radiance);
			writer.imageArray(set, k_down_child_ambient_binding, sampled, child_ambient);
			writer.image(set, k_down_opacity_binding, image, nullptr, *top.opacity_mip_views[mip - 1], general);
			writer.imageArray(set, k_down_radiance_binding, image, radiance);
			writer.imageArray(set, k_down_ambient_binding, image, ambient);
		}
	}
	writer.flush(core.getDevice());
	m_debug_bound.invalidate();
	bumpTargetGeneration();
	TOAST_INFO(
	    "Render",
	    "GiClipmapPass {} levels of {} cells a side ({} MB)",
	    levels,
	    resolution,
	    (static_cast<uint64_t>(resolution) * resolution * resolution * 64 * levels) >> 20
	);
}

void GiClipmapPass::gatherBodies(const RenderFrame& frame) {
	const auto add = [&](glm::vec3 low, glm::vec3 high) {
		if (m_body_boxes.size() < 2 * k_max_body_boxes) {
			m_body_boxes.push_back(low);
			m_body_boxes.push_back(high);
			return;
		}
		auto last = m_body_boxes.end() - 2;
		last[0] = glm::min(last[0], low);
		last[1] = glm::max(last[1], high);
	};
	const auto sphere = [&](const glm::vec4& bounds) { add(glm::vec3(bounds) - bounds.w, glm::vec3(bounds) + bounds.w); };

	m_bodies_next.clear();
	for (const auto& proxy : frame.voxel_instances) {
		const Body body {.model = proxy.model, .bounds = glm::vec4(proxy.bounds_center, proxy.bounds_radius)};
		m_bodies_next.emplace(proxy.node_rid, body);
		const auto found = m_bodies.find(proxy.node_rid);
		if (found == m_bodies.end()) {
			sphere(body.bounds);
		} else if (found->second.model != body.model) {
			sphere(found->second.bounds);
			sphere(body.bounds);
		}
	}
	for (const auto& [rid, body] : m_bodies) {
		if (!m_bodies_next.contains(rid)) {
			sphere(body.bounds);
		}
	}
	std::swap(m_bodies, m_bodies_next);
}

auto GiClipmapPass::ambientViews() const -> std::array<vk::ImageView, k_max_levels * k_faces> {
	std::array<vk::ImageView, k_max_levels * k_faces> views;
	for (uint32_t i = 0; i < k_max_levels; ++i) {
		for (uint32_t f = 0; f < k_faces; ++f) {
			views[(i * k_faces) + f] =
			    m_level_count == 0 ? **m_dummy_view : **m_levels[i < m_level_count ? i : 0].ambient[f].sampled_view;
		}
	}
	return views;
}

auto GiClipmapPass::radianceViews() const -> std::array<vk::ImageView, k_max_levels * k_faces> {
	std::array<vk::ImageView, k_max_levels * k_faces> views;
	for (uint32_t i = 0; i < k_max_levels; ++i) {
		for (uint32_t f = 0; f < k_faces; ++f) {
			views[(i * k_faces) + f] =
			    m_level_count == 0 ? **m_dummy_view : **m_levels[i < m_level_count ? i : 0].faces[f].sampled_view;
		}
	}
	return views;
}

auto GiClipmapPass::opacityViews() const -> std::array<vk::ImageView, k_max_levels> {
	std::array<vk::ImageView, k_max_levels> views;
	for (uint32_t i = 0; i < k_max_levels; ++i) {
		const Level& level = m_levels[i < m_level_count ? i : 0];
		views[i] = m_level_count == 0 ? **m_dummy_view
		                              : (level.opacity_all_view.has_value() ? **level.opacity_all_view : **level.opacity_view);
	}
	return views;
}

void GiClipmapPass::unpublish(vk::CommandBuffer cmd) {
	if (!m_published_valid) {
		return;
	}
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

void GiClipmapPass::markFull() {
	for (Level& level : m_levels) {
		level.needs_full = true;
	}
}

void GiClipmapPass::onResize(vk::Extent2D extent) {
	m_extent = extent;
	if (m_core != nullptr && m_debug_pipeline.isReady()) {
		m_debug_target.create(*m_core, extent, m_scene_format, "GiClipmapPass Debug");
		m_debug_bound.invalidate();
	}
}

auto GiClipmapPass::record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView {
	ZoneScoped;
	auto* renderer = VulkanRenderer::instance;
	const auto* frame = renderer->renderingFrame();
	const GiSettings& settings = renderer->giSettings();
	constexpr auto relaxed = std::memory_order_relaxed;
	if (!settings.enabled.load(relaxed) || settings.backend.load(relaxed) != GiSettings::k_backend_clipmap || frame == nullptr ||
	    frame_index >= m_storage_sets.size() || !m_fill_pipeline.isReady()) {
		markFull();
		unpublish(cmd);
		return source_view;
	}

	gatherBodies(*frame);
	// Bodies are tracked above against the last fill so only edits and new bodies come from here
	m_dirty_boxes.gather(*frame, std::numeric_limits<float>::max());
	GiDirtyBoxes::Counts counts;
	const bool known = m_dirty_boxes.take(counts);

	const VoxelTraceScene* trace = renderer->voxelTraceScene();
	if (trace == nullptr || !frame->voxel_storage || !frame->voxel_storage->isReady()) {
		m_dirty_boxes.clear();
		m_body_boxes.clear();
		markFull();
		unpublish(cmd);
		return source_view;
	}

	struct Tier {
		uint32_t resolution;
		uint32_t levels;
	};

	constexpr std::array<Tier, GiSettings::k_quality_custom> k_tiers {
	  {{64, 3}, {96, 4}, {128, 4}, {128, 5}}
	};
	const uint32_t quality = std::min(settings.quality.load(relaxed), GiSettings::k_quality_custom);
	const bool custom = quality == GiSettings::k_quality_custom;
	const uint32_t resolution =
	    custom ? std::clamp(settings.clip_resolution.load(relaxed), 32u, 256u) : k_tiers[quality].resolution;
	const uint32_t level_count =
	    custom ? std::clamp(settings.clip_levels.load(relaxed), 1u, k_max_levels) : k_tiers[quality].levels;
	if (resolution != m_resolution || level_count != m_level_count) {
		createLevels(resolution, level_count);
		markFull();
	}
	const float cell = std::clamp(settings.clip_cell.load(relaxed), 0.05f, 4.0f);
	if (cell != m_cell) {
		m_cell = cell;
		markFull();
	}

	// Safe since the fence of this slot was waited on
	if (m_storage_bound.update(frame_index, frame->voxel_storage)) {
		const vk::DescriptorSet set = *m_storage_sets[frame_index];
		writeVoxelStorageDescriptors(m_core->getDevice(), set, *frame->voxel_storage);
		constexpr auto storage = vk::DescriptorType::eStorageBuffer;
		DescriptorWriter writer;
		writer.buffer(set, k_trace_volumes_binding, storage, trace->volumeBuffer(frame_index));
		writer.buffer(set, k_trace_bounds_binding, storage, trace->boundsBuffer(frame_index));
		writer.buffer(set, k_trace_grid_binding, storage, trace->gridBuffer(frame_index));
		writer.flush(m_core->getDevice());
	}

	// Before the fill so the clipmap is still the one lighting read last frame and its published frame addresses it
	const int bounce_slab = std::max(static_cast<int>(resolution) / static_cast<int>(k_inject_period), 1);
	const int bounce_phase = static_cast<int>(m_frame % k_inject_period);
	if (m_published_valid && m_bounce_pipeline.isReady() && m_bounce_list_pipeline.isReady() && !m_bounce_scene_sets.empty() &&
	    !m_levels[0].needs_full) {
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "GI clip bounce");
		sync::BarrierBatch barriers;
		for (uint32_t i = 0; i < level_count; ++i) {
			for (Face& face : m_levels[i].ambient) {
				barriers.image(face.state, sync::Usage::compute_storage_write);
			}
		}
		barriers.flush(cmd);
		m_bounce_scene_sets.updateTlas(frame_index);
		const vk::PipelineLayout layout = *m_bounce_layout.getPipelineLayout();
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eCompute,
		    layout,
		    k_bounce_scene_set,
		    std::array<vk::DescriptorSet, 1> {m_bounce_scene_sets.get(frame_index)},
		    {}
		);
		const int bounce_side = static_cast<int>(resolution);
		for (uint32_t i = 0; i < level_count; ++i) {
			const glm::ivec3 origin = m_levels[i].origin;
			glm::ivec3 low = origin;
			glm::ivec3 size(bounce_side);
			low.z = origin.z + (bounce_phase * bounce_slab);
			size.z = bounce_phase + 1 == static_cast<int>(k_inject_period) ? (origin.z + bounce_side) - low.z : bounce_slab;
			cmd.bindDescriptorSets(
			    vk::PipelineBindPoint::eCompute,
			    layout,
			    k_bounce_set,
			    std::array<vk::DescriptorSet, 1> {*m_bounce_sets[(frame_index * k_max_levels) + i]},
			    {}
			);
			const FillPush push {.box_min = glm::ivec4(low, static_cast<int>(i)), .box_size = glm::ivec4(size, 0)};
			cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), &push);

			barriers.memory(sync::Usage::compute_storage_write, sync::Usage::transfer_dst);
			barriers.flush(cmd);
			cmd.fillBuffer(**m_bounce_list, 0, sizeof(uint32_t) * k_bounce_header, 0u);
			barriers.memory(sync::Usage::transfer_dst, sync::Usage::compute_storage_write);
			barriers.flush(cmd);
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_bounce_list_pipeline.getPipeline());
			const glm::ivec3 groups = (size + (k_group - 1)) / k_group;
			cmd.dispatch(static_cast<uint32_t>(groups.x), static_cast<uint32_t>(groups.y), static_cast<uint32_t>(groups.z));

			// Every group past the count leaves at once so the full capacity is dispatched without a readback
			barriers.memory(sync::Usage::compute_storage_write, sync::Usage::compute_storage_write);
			barriers.flush(cmd);
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_bounce_pipeline.getPipeline());
			cmd.dispatch(k_bounce_capacity / k_bounce_group, 1, 1);
		}
	}

	const glm::vec3 camera = frame->frame_data.camera_position;
	FrameData data {
	  .inverse_view_projection = glm::inverse(frame->frame_data.view_projection),
	  .camera = glm::vec4(camera, 0.0f),
	  .size = glm::uvec4(resolution, level_count, 1u, m_tail),
	};

	const int side = static_cast<int>(resolution);
	std::vector<glm::vec3> dirty = std::move(m_body_boxes);
	m_body_boxes.clear();
	dirty.insert(dirty.end(), m_dirty_boxes.boxes().begin(), m_dirty_boxes.boxes().end());
	std::array<std::vector<gi_clip::Box>, k_max_levels> boxes;
	std::array<bool, k_max_levels> whole {};
	std::array<size_t, k_max_levels> scrolled_boxes {};
	bool any = false;
	for (uint32_t i = 0; i < level_count; ++i) {
		Level& level = m_levels[i];
		const float level_cell = cell * static_cast<float>(1u << i);
		const glm::ivec3 origin = gi_clip::origin(camera, level_cell, side);
		data.levels[i] = glm::vec4(glm::vec3(origin), level_cell);
		if (level.needs_full || !known) {
			boxes[i].push_back({.min = origin, .max = origin + side});
			whole[i] = true;
			scrolled_boxes[i] = 1;
		} else {
			std::array<gi_clip::Box, 3> slabs;
			const uint32_t count = gi_clip::scrolled(level.origin, origin, side, slabs);
			boxes[i].assign(slabs.begin(), slabs.begin() + count);
			scrolled_boxes[i] = count;
			for (size_t b = 0; b + 1 < dirty.size(); b += 2) {
				const gi_clip::Box box = gi_clip::cells(dirty[b], dirty[b + 1], level_cell, origin, side);
				if (!gi_clip::empty(box)) {
					boxes[i].push_back(box);
				}
			}
		}
		level.origin = origin;
		level.needs_full = false;
		any = any || !boxes[i].empty();
	}
	m_dirty_boxes.clear();

	const auto& allocation = m_frame_data[frame_index].getAllocation();
	std::memcpy(allocation.getInfo().pMappedData, &data, sizeof(data));
	allocation.flush(0, sizeof(data));

	if (any) {
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "GI clip fill");
		sync::BarrierBatch barriers;
		for (uint32_t i = 0; i < level_count; ++i) {
			if (!boxes[i].empty()) {
				barriers.image(m_levels[i].material_state, sync::Usage::compute_storage_write);
				barriers.image(m_levels[i].emission_state, sync::Usage::compute_storage_write);
				barriers.image(m_levels[i].opacity_state, sync::Usage::compute_storage_write);
				for (Face& face : m_levels[i].ambient) {
					barriers.image(face.state, sync::Usage::compute_storage_write);
				}
			}
		}
		barriers.flush(cmd);

		const vk::PipelineLayout layout = *m_fill_layout.getPipelineLayout();
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_fill_pipeline.getPipeline());
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eCompute,
		    layout,
		    k_storage_set,
		    std::array<vk::DescriptorSet, 1> {*m_storage_sets[frame_index]},
		    {}
		);
		for (uint32_t i = 0; i < level_count; ++i) {
			if (boxes[i].empty()) {
				continue;
			}
			cmd.bindDescriptorSets(
			    vk::PipelineBindPoint::eCompute,
			    layout,
			    k_fill_set,
			    std::array<vk::DescriptorSet, 1> {*m_fill_sets[(frame_index * k_max_levels) + i]},
			    {}
			);
			for (size_t b = 0; b < boxes[i].size(); ++b) {
				const gi_clip::Box& box = boxes[i][b];
				const glm::ivec3 size = box.max - box.min;
				const FillPush push {
				  .box_min = glm::ivec4(box.min, static_cast<int>(i)),
				  .box_size = glm::ivec4(size, b < scrolled_boxes[i] ? 1 : 0),
				};
				cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), &push);
				const glm::ivec3 groups = (size + (k_group - 1)) / k_group;
				cmd.dispatch(static_cast<uint32_t>(groups.x), static_cast<uint32_t>(groups.y), static_cast<uint32_t>(groups.z));
			}
		}
	}

	std::array<std::vector<gi_clip::Box>, k_max_levels> injected;
	if (m_inject_pipeline.isReady() && !m_inject_scene_sets.empty()) {
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "GI clip inject");
		sync::BarrierBatch barriers;
		for (uint32_t i = 0; i < level_count; ++i) {
			barriers.image(m_levels[i].material_state, sync::Usage::shader_sampled);
			barriers.image(m_levels[i].emission_state, sync::Usage::shader_sampled);
			barriers.image(m_levels[i].opacity_state, sync::Usage::shader_sampled);
			for (Face& face : m_levels[i].faces) {
				barriers.image(face.state, sync::Usage::compute_storage_write);
			}
			for (Face& face : m_levels[i].ambient) {
				barriers.image(face.state, sync::Usage::shader_sampled);
			}
		}
		barriers.flush(cmd);

		m_inject_scene_sets.updateTlas(frame_index);
		const vk::PipelineLayout layout = *m_inject_layout.getPipelineLayout();
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_inject_pipeline.getPipeline());
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eCompute,
		    layout,
		    k_inject_scene_set,
		    std::array<vk::DescriptorSet, 1> {m_inject_scene_sets.get(frame_index)},
		    {}
		);
		const int slab = std::max(side / static_cast<int>(k_inject_period), 1);
		const int phase = static_cast<int>(m_frame++ % k_inject_period);
		for (uint32_t i = 0; i < level_count; ++i) {
			std::vector<gi_clip::Box>& inject = injected[i];
			const glm::ivec3 origin = m_levels[i].origin;
			const gi_clip::Box level_box {.min = origin, .max = origin + side};
			if (whole[i]) {
				inject.push_back(level_box);
			} else {
				// One cell wider since a changed cell changes which faces of its neighbours are exposed
				for (const gi_clip::Box& box : boxes[i]) {
					inject.push_back({.min = glm::max(box.min - 1, level_box.min), .max = glm::min(box.max + 1, level_box.max)});
				}
				// The rest relights round robin so a far occluder change still arrives
				gi_clip::Box round = level_box;
				round.min.z = origin.z + (phase * slab);
				round.max.z = phase + 1 == static_cast<int>(k_inject_period) ? level_box.max.z : round.min.z + slab;
				inject.push_back(round);
			}
			cmd.bindDescriptorSets(
			    vk::PipelineBindPoint::eCompute,
			    layout,
			    k_inject_set,
			    std::array<vk::DescriptorSet, 1> {*m_inject_sets[(frame_index * k_max_levels) + i]},
			    {}
			);
			for (const gi_clip::Box& box : inject) {
				const glm::ivec3 size = box.max - box.min;
				const FillPush push {.box_min = glm::ivec4(box.min, static_cast<int>(i)), .box_size = glm::ivec4(size, 0)};
				cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), &push);
				const glm::ivec3 groups = (size + (k_group - 1)) / k_group;
				cmd.dispatch(static_cast<uint32_t>(groups.x), static_cast<uint32_t>(groups.y), static_cast<uint32_t>(groups.z));
			}
		}
	}

	std::vector<gi_clip::Box> written;
	if (m_down_pipeline.isReady() && m_inject_pipeline.isReady() && !m_inject_scene_sets.empty() && level_count > 1) {
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "GI clip downsample");
		const vk::PipelineLayout layout = *m_down_layout.getPipelineLayout();
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_down_pipeline.getPipeline());
		std::vector<gi_clip::Box> parents;
		for (uint32_t i = 0; i + 1 < level_count; ++i) {
			// The finer box in cells of the level written which is whole cells since level origins are even
			const glm::ivec3 inner_min = m_levels[i].origin >> 1;
			const gi_clip::Box inner {.min = inner_min, .max = inner_min + (side / 2)};
			parents.clear();
			if (whole[i] || whole[i + 1]) {
				parents.push_back(inner);
			} else {
				const auto add = [&](const gi_clip::Box& box) {
					const gi_clip::Box parent {.min = glm::max(box.min >> 1, inner.min), .max = glm::min((box.max + 1) >> 1, inner.max)};
					if (!gi_clip::empty(parent)) {
						parents.push_back(parent);
					}
				};
				std::ranges::for_each(injected[i], add);
				std::ranges::for_each(written, add);
				if (parents.size() > k_max_down_boxes) {
					gi_clip::Box bounds = parents.front();
					for (const gi_clip::Box& box : parents) {
						bounds.min = glm::min(bounds.min, box.min);
						bounds.max = glm::max(bounds.max, box.max);
					}
					parents.assign(1, bounds);
				}
			}
			written = parents;
			if (parents.empty()) {
				continue;
			}
			sync::BarrierBatch barriers;
			barriers.image(m_levels[i].opacity_state, sync::Usage::shader_sampled);
			barriers.image(m_levels[i + 1].opacity_state, sync::Usage::compute_storage_write);
			for (uint32_t f = 0; f < k_faces; ++f) {
				barriers.image(m_levels[i].faces[f].state, sync::Usage::shader_sampled);
				barriers.image(m_levels[i].ambient[f].state, sync::Usage::shader_sampled);
				barriers.image(m_levels[i + 1].faces[f].state, sync::Usage::compute_storage_write);
				barriers.image(m_levels[i + 1].ambient[f].state, sync::Usage::compute_storage_write);
			}
			barriers.flush(cmd);
			cmd.bindDescriptorSets(
			    vk::PipelineBindPoint::eCompute,
			    layout,
			    0,
			    std::array<vk::DescriptorSet, 1> {*m_down_sets[(frame_index * k_max_levels) + i + 1]},
			    {}
			);
			for (const gi_clip::Box& box : parents) {
				const glm::ivec3 size = box.max - box.min;
				const DownPush push {.box_min = glm::ivec4(box.min, side), .box_size = glm::ivec4(size, side)};
				cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), &push);
				const glm::ivec3 groups = (size + (k_group - 1)) / k_group;
				cmd.dispatch(static_cast<uint32_t>(groups.x), static_cast<uint32_t>(groups.y), static_cast<uint32_t>(groups.z));
			}
		}
	}

	if (m_mip_pipeline.isReady() && m_tail > 0 && m_inject_pipeline.isReady() && !m_inject_scene_sets.empty()) {
		const GpuScope scope(renderer->gpuTimer(), frame_index, cmd, "GI clip mips");
		const uint32_t last = level_count - 1;
		Level& top = m_levels[last];
		std::vector<gi_clip::Box> changed = level_count > 1 ? written : std::vector<gi_clip::Box> {};
		changed.insert(changed.end(), injected[last].begin(), injected[last].end());
		sync::BarrierBatch barriers;
		barriers.image(top.opacity_state, sync::Usage::general);
		for (Face& face : top.faces) {
			barriers.image(face.state, sync::Usage::general);
		}
		barriers.flush(cmd);
		const vk::PipelineLayout layout = *m_down_layout.getPipelineLayout();
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_mip_pipeline.getPipeline());
		std::vector<gi_clip::Box> parents;
		for (uint32_t mip = 1; mip <= m_tail; ++mip) {
			const int parent_side = side >> mip;
			parents.clear();
			if (whole[last]) {
				// Any run of that many cells covers every texel
				const glm::ivec3 low = top.origin >> static_cast<int>(mip);
				parents.push_back({.min = low, .max = low + parent_side});
			} else {
				for (const gi_clip::Box& box : changed) {
					gi_clip::Box parent {.min = box.min >> 1, .max = (box.max + 1) >> 1};
					parent.max = glm::min(parent.max, parent.min + parent_side);
					if (!gi_clip::empty(parent)) {
						parents.push_back(parent);
					}
				}
				if (parents.size() > k_max_down_boxes) {
					gi_clip::Box bounds = parents.front();
					for (const gi_clip::Box& box : parents) {
						bounds.min = glm::min(bounds.min, box.min);
						bounds.max = glm::max(bounds.max, box.max);
					}
					bounds.max = glm::min(bounds.max, bounds.min + parent_side);
					parents.assign(1, bounds);
				}
			}
			changed = parents;
			if (parents.empty()) {
				break;
			}
			barriers.memory(sync::Usage::general, sync::Usage::general);
			barriers.flush(cmd);
			cmd.bindDescriptorSets(
			    vk::PipelineBindPoint::eCompute,
			    layout,
			    0,
			    std::array<vk::DescriptorSet, 1> {*m_mip_sets[(frame_index * k_max_tail) + mip - 1]},
			    {}
			);
			for (const gi_clip::Box& box : parents) {
				const glm::ivec3 size = box.max - box.min;
				const DownPush push {
				  .box_min = glm::ivec4(box.min, parent_side),
				  .box_size = glm::ivec4(size, parent_side * 2),
				  .mip = glm::ivec4(static_cast<int>(mip) - 1, 0, 0, 0),
				};
				cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), &push);
				const glm::ivec3 groups = (size + (k_group - 1)) / k_group;
				cmd.dispatch(static_cast<uint32_t>(groups.x), static_cast<uint32_t>(groups.y), static_cast<uint32_t>(groups.z));
			}
		}
	}

	// Lighting of the next frame reads these so they end the frame sampled with the frame that addresses them
	if (m_inject_pipeline.isReady() && !m_inject_scene_sets.empty()) {
		sync::BarrierBatch barriers;
		for (uint32_t i = 0; i < level_count; ++i) {
			barriers.image(m_levels[i].material_state, sync::Usage::shader_sampled);
			barriers.image(m_levels[i].opacity_state, sync::Usage::shader_sampled);
			for (Face& face : m_levels[i].faces) {
				barriers.image(face.state, sync::Usage::shader_sampled);
			}
			for (Face& face : m_levels[i].ambient) {
				barriers.image(face.state, sync::Usage::shader_sampled);
			}
		}
		barriers.memory(sync::Usage::transfer_dst, sync::Usage::transfer_dst);
		barriers.memory(sync::Usage::fragment_storage_read, sync::Usage::transfer_dst);
		barriers.memory(sync::Usage::compute_storage_read, sync::Usage::transfer_dst);
		barriers.flush(cmd);
		cmd.copyBuffer(*m_frame_data[frame_index], **m_published, vk::BufferCopy(0, 0, sizeof(FrameData)));
		barriers.memory(sync::Usage::transfer_dst, sync::Usage::fragment_storage_read);
		barriers.memory(sync::Usage::transfer_dst, sync::Usage::compute_storage_read);
		barriers.flush(cmd);
		m_published_valid = true;
	}

	const uint32_t debug_view = settings.clip_debug_view.load(relaxed);
	if (debug_view == 0 || !source_view || !m_debug_pipeline.isReady() || !m_debug_target.isReady()) {
		return source_view;
	}
	return recordDebug(
	    cmd,
	    frame_index,
	    source_view,
	    std::min(debug_view - 1, level_count - 1),
	    std::min(settings.clip_debug_mode.load(relaxed), 3u)
	);
}

auto GiClipmapPass::recordDebug(
    vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view, uint32_t level_index, uint32_t mode
) -> vk::ImageView {
	ZoneScoped;
	Level& level = m_levels[level_index];
	sync::BarrierBatch barriers;
	barriers.image(level.material_state, sync::Usage::shader_sampled);
	barriers.image(level.emission_state, sync::Usage::shader_sampled);
	for (Face& face : level.faces) {
		barriers.image(face.state, sync::Usage::shader_sampled);
	}
	barriers.flush(cmd);

	const std::array<vk::ImageView, 4> wanted {
	  source_view, **level.material_view, **level.emission_view, **level.faces[0].sampled_view
	};
	if (m_debug_bound.update(frame_index, wanted)) {
		constexpr auto sampled = vk::DescriptorType::eCombinedImageSampler;
		constexpr auto read = vk::ImageLayout::eShaderReadOnlyOptimal;
		const vk::DescriptorSet set = *m_debug_sets[frame_index];
		DescriptorWriter writer;
		writer.image(set, k_debug_source_binding, sampled, *m_point_sampler, source_view, read);
		writer.image(set, k_debug_material_binding, sampled, *m_point_sampler, **level.material_view, read);
		writer.image(set, k_debug_emission_binding, sampled, *m_point_sampler, **level.emission_view, read);
		std::array<vk::DescriptorImageInfo, k_faces> faces;
		for (uint32_t f = 0; f < k_faces; ++f) {
			faces[f] = vk::DescriptorImageInfo(*m_point_sampler, **level.faces[f].sampled_view, read);
		}
		writer.imageArray(set, k_debug_radiance_binding, sampled, faces);
		writer.flush(m_core->getDevice());
	}

	const vk::PipelineLayout layout = *m_debug_layout.getPipelineLayout();
	m_debug_target.beginScope(cmd);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics, layout, 0, std::array<vk::DescriptorSet, 1> {*m_debug_sets[frame_index]}, {}
	);
	const std::array push {level_index, mode};
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(push), push.data());
	cmd.draw(3, 1, 0, 0);
	m_debug_target.endScope(cmd);
	return m_debug_target.view();
}

}
