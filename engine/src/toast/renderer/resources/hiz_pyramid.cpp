#include "toast/renderer/resources/hiz_pyramid.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/vulkan_core.hpp"
#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr vk::Format k_format = vk::Format::eR32Sfloat;
/// Level 0 texels per group side in hiz_reduce.slang
constexpr uint32_t k_tile = 32;
constexpr uint32_t k_depth_binding = 0;
constexpr uint32_t k_levels_binding = 1;
constexpr uint32_t k_counter_binding = 2;

auto halved(vk::Extent2D extent, uint32_t level) -> vk::Extent2D {
	const uint32_t shift = level + 1;
	const uint32_t round = (1u << shift) - 1u;
	return {(extent.width + round) >> shift, (extent.height + round) >> shift};
}

auto levelRange(uint32_t level, uint32_t count = 1) -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eColor, level, count, 0, 1};
}

}

void HiZPyramid::buildPipeline(const VulkanCore& core) {
	const auto uid = assets::resolveURI("core://shaders/hiz_reduce.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_WARN("Render", "HiZPyramid shader core://shaders/hiz_reduce.slang unavailable; occlusion culling is off");
		return;
	}
	m_layout.rebuild(core, shader->reflection, "HiZReduce");
	if (m_layout.getDescriptorSetLayouts().empty()) {
		TOAST_WARN("Render", "HiZReduce shader layout has no descriptor sets; occlusion culling is off");
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::compute;
	config.debug_name = "HiZReduce";
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_layout.getPipelineLayout();
	m_pipeline.rebuild(core, config);
}

auto HiZPyramid::levelCount(vk::Extent2D extent) -> uint32_t {
	const vk::Extent2D first = halved(extent, 0);
	return std::min(1u + static_cast<uint32_t>(std::bit_width(std::max({first.width, first.height, 1u}) - 1u)), k_max_levels);
}

void HiZPyramid::create(const VulkanCore& core, vk::ImageView depth, vk::Extent2D depth_extent) {
	ZoneScoped;
	m_set.reset();
	m_level_views.clear();
	m_view.reset();
	m_image.reset();
	m_state = {};
	m_built_extent = vk::Extent2D {0, 0};
	m_built_levels = 0;
	if (!depth || depth_extent.width == 0 || depth_extent.height == 0) {
		return;
	}

	if (!m_counter.has_value()) {
		vk::BufferCreateInfo counter_ci {};
		counter_ci.size = sizeof(uint32_t);
		counter_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
		vma::AllocationCreateInfo counter_alloc {};
		counter_alloc.usage = vma::MemoryUsage::eAutoPreferDevice;
		m_counter.emplace(core.getAllocator().createBuffer(counter_ci, counter_alloc));
		setDebugName(core, **m_counter, "HiZPyramid Counter");
		m_counter_cleared = false;
	}

	// The image exists without the pipeline so a cull shader binding it always sees a valid view
	if (!*m_sampler) {
		m_sampler = vk::raii::Sampler(core.getDevice(), nearestClampSamplerInfo());
		setDebugName(core, *m_sampler, "HiZPyramid Sampler");
	}
	if (!m_pipeline.isReady()) {
		buildPipeline(core);
	}

	m_levels = levelCount(depth_extent);
	// Halving rounds up while mips round down so a power of two base holds every level at full size
	const vk::Extent2D halved_first = halved(depth_extent, 0);
	const vk::Extent2D first {std::bit_ceil(halved_first.width), std::bit_ceil(halved_first.height)};

	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = k_format;
	image_ci.extent = vk::Extent3D {first.width, first.height, 1};
	image_ci.mipLevels = m_levels;
	image_ci.arrayLayers = 1;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_image.emplace(core.getAllocator().createImage(image_ci, alloc_ci));
	setDebugName(core, **m_image, "HiZPyramid");
	m_state.reset(**m_image, levelRange(0, m_levels));

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = k_format;
	view_ci.subresourceRange = levelRange(0, m_levels);
	m_view.emplace(core.getDevice(), view_ci);
	setDebugName(core, **m_view, "HiZPyramid View");

	for (uint32_t level = 0; level < m_levels; ++level) {
		view_ci.subresourceRange = levelRange(level);
		m_level_views.emplace_back(core.getDevice(), view_ci);
		setDebugName(core, *m_level_views.back(), std::format("HiZPyramid Level[{}]", level));
	}

	if (!m_pipeline.isReady()) {
		return;
	}
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout set_layout = *m_layout.getDescriptorSetLayouts()[0];
	auto allocated = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
	m_set.emplace(std::move(allocated[0]));
	setDebugName(core, **m_set, "HiZPyramid Set");

	// Unused slots repeat the last level since the whole array must hold valid views
	std::array<vk::DescriptorImageInfo, k_max_levels> levels {};
	for (uint32_t level = 0; level < k_max_levels; ++level) {
		levels[level] = vk::DescriptorImageInfo(nullptr, *m_level_views[std::min(level, m_levels - 1)], vk::ImageLayout::eGeneral);
	}

	DescriptorWriter writer;
	writer.image(
	    **m_set,
	    k_depth_binding,
	    vk::DescriptorType::eCombinedImageSampler,
	    *m_sampler,
	    depth,
	    vk::ImageLayout::eDepthReadOnlyOptimal
	);
	writer.imageArray(**m_set, k_levels_binding, vk::DescriptorType::eStorageImage, levels);
	writer.buffer(**m_set, k_counter_binding, vk::DescriptorType::eStorageBuffer, **m_counter);
	writer.flush(core.getDevice());
}

void HiZPyramid::beforeRead(sync::BarrierBatch& barriers) {
	barriers.image(m_state, sync::Usage::compute_sampled);
}

void HiZPyramid::record(vk::CommandBuffer cmd, vk::Extent2D scene_extent, const glm::mat4& view_projection) {
	ZoneScoped;
	if (!isReady()) {
		return;
	}
	const uint32_t levels = std::min(levelCount(scene_extent), m_levels);

	// Earlier reads of the previous pyramid finish before it is overwritten
	sync::BarrierBatch barriers;
	barriers.image(m_state, sync::Usage::compute_storage_write);
	if (!m_counter_cleared) {
		cmd.fillBuffer(**m_counter, 0, sizeof(uint32_t), 0);
		barriers.buffer(**m_counter, sync::Usage::transfer_dst, sync::Usage::compute_storage_write);
		m_counter_cleared = true;
	}
	barriers.flush(cmd);

	const vk::Extent2D first = halved(scene_extent, 0);
	const uint32_t groups_x = (first.width + k_tile - 1) / k_tile;
	const uint32_t groups_y = (first.height + k_tile - 1) / k_tile;
	const ReducePush push {
	  .source_size = {scene_extent.width, scene_extent.height},
	  .level_count = levels,
	  .group_count = groups_x * groups_y,
	};

	const vk::PipelineLayout layout = *m_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 0, std::array {**m_set}, {});
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(ReducePush), &push);
	cmd.dispatch(groups_x, groups_y, 1);

	barriers.image(m_state, sync::Usage::compute_sampled);
	barriers.flush(cmd);

	m_built_extent = scene_extent;
	m_built_levels = levels;
	m_built_view_projection = view_projection;
}

}
