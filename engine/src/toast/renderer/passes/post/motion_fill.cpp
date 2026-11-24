/**
 * @file motion_fill.cpp
 * @author dario
 * @date 22/11/2026
 */

#include "toast/renderer/passes/post/motion_fill.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

namespace renderer {

namespace {

constexpr uint32_t k_group_size = 8;

}

MotionFill::MotionFill(const VulkanCore& core, vk::Extent2D render_extent) : m_core(&core), m_extent(render_extent) {
	ZoneScoped;
	static_assert(sizeof(Push) <= 128, "Vulkan guarantees 128 bytes of push constants");

	const auto uid = assets::resolveURI("core://shaders/motion_fill.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "MotionFill shader core://shaders/motion_fill.slang unavailable; motion fill is disabled");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "MotionFill");
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "MotionFill shader layout has no descriptor sets");
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::compute;
	config.debug_name = "MotionFill";
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	m_pipeline.rebuild(core, config);

	// Nearest since texel-exact reads must not blend across edges
	m_point_sampler = vk::raii::Sampler(core.getDevice(), nearestClampSamplerInfo());
	setDebugName(core, *m_point_sampler, "MotionFill Sampler");

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	m_bound_views.resize(VulkanRenderer::k_frames_in_flight);
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto allocated = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
		m_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_descriptor_sets[i], std::format("MotionFill DescriptorSet[{}]", i));
	}

	onResize(render_extent);
}

void MotionFill::createTarget(Target& target, vk::Format format, std::string_view debug_name) const {
	// View before the image it views
	target.view.reset();
	target.image.reset();
	target.state = {};

	const auto image_ci = colorTargetImageInfo(m_extent, format, vk::ImageUsageFlagBits::eStorage);
	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	target.image.emplace(m_core->getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(*m_core, **target.image, std::format("{} Image", debug_name));
	target.state.reset(**target.image, colorSubresourceRange());

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **target.image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = format;
	view_ci.subresourceRange = colorSubresourceRange();
	target.view.emplace(m_core->getDevice(), view_ci);
	setDebugName(*m_core, **target.view, std::format("{} View", debug_name));
}

void MotionFill::onResize(vk::Extent2D render_extent) {
	ZoneScoped;
	if (m_core == nullptr || !m_pipeline.isReady()) {
		return;
	}
	m_extent = render_extent;
	createTarget(m_motion, vk::Format::eR16G16B16A16Sfloat, "MotionFill Motion");
	createTarget(m_reactive, vk::Format::eR8Unorm, "MotionFill Reactive");
	m_bound_views.invalidate();
}

auto MotionFill::record(vk::CommandBuffer cmd, uint32_t frame_index, const glm::mat4& reprojection, bool ignore_written) -> bool {
	ZoneScoped;
	const vk::ImageView depth_view = VulkanRenderer::instance->getDepthView();
	const vk::ImageView scene_motion = VulkanRenderer::instance->getSceneMotionView();
	if (!m_pipeline.isReady() || frame_index >= m_descriptor_sets.size() || !m_motion.view || !m_reactive.view || !depth_view ||
	    !scene_motion) {
		return false;
	}

	// The renderer already made depth and scene motion sampleable by compute before the post passes
	sync::BarrierBatch barriers;
	barriers.image(m_motion.state, sync::Usage::compute_storage_write);
	barriers.image(m_reactive.state, sync::Usage::compute_storage_write);
	barriers.flush(cmd);

	const std::array<vk::ImageView, 4> wanted {depth_view, scene_motion, **m_motion.view, **m_reactive.view};
	if (m_bound_views.update(frame_index, wanted)) {
		const vk::DescriptorSet set = *m_descriptor_sets[frame_index];
		DescriptorWriter writer;
		// Depth stays eDepthReadOnlyOptimal for the overlay stage
		writer.image(
		    set, 0, vk::DescriptorType::eCombinedImageSampler, *m_point_sampler, depth_view, vk::ImageLayout::eDepthReadOnlyOptimal
		);
		writer.image(
		    set, 1, vk::DescriptorType::eCombinedImageSampler, *m_point_sampler, scene_motion, vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.image(set, 2, vk::DescriptorType::eStorageImage, nullptr, **m_motion.view, vk::ImageLayout::eGeneral);
		writer.image(set, 3, vk::DescriptorType::eStorageImage, nullptr, **m_reactive.view, vk::ImageLayout::eGeneral);
		writer.flush(m_core->getDevice());
	}

	Push push {};
	push.reprojection = reprojection;
	push.width = m_extent.width;
	push.height = m_extent.height;
	push.ignore_written = ignore_written ? 1u : 0u;

	const vk::PipelineLayout layout = *m_shader_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, layout, 0, std::array {*m_descriptor_sets[frame_index]}, {});
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(Push), &push);
	cmd.dispatch((m_extent.width + k_group_size - 1) / k_group_size, (m_extent.height + k_group_size - 1) / k_group_size, 1);

	// Fragment and compute readers both sample these later
	barriers.image(m_motion.state, sync::Usage::shader_sampled);
	barriers.image(m_reactive.state, sync::Usage::shader_sampled);
	barriers.flush(cmd);
	return true;
}

}
