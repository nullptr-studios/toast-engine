#include "toast/renderer/passes/geometry/debris_pass.hpp"

#include "toast/renderer/core/descriptor_writer.hpp"
#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/shaders/shader_cache.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <toast/physics/debris_simulation.hpp>
#include <toast/voxel/voxel_constants.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_debris_scene_set = 0;
constexpr uint32_t k_debris_set = 1;
constexpr uint32_t k_debris_particles_binding = 0;
constexpr uint32_t k_debris_list_binding = 1;
constexpr uint32_t k_debris_backdrop_set = 2;
constexpr uint32_t k_debris_backdrop_binding = 0;
constexpr uint32_t k_debris_backdrop_depth_binding = 1;

/// Bounds and cache signature only, the rest comes from the push constants
auto shadowBoundsOf(const physics::DebrisSimulation& simulation, const AsyncComputeTask::FrameView& view, float extrapolation)
    -> DebrisPass::ShadowDraw;

}

DebrisPass::DebrisPass(
    const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent,
    std::shared_ptr<physics::DebrisSimulation> simulation
)
    : m_core(&core),
      m_simulation(std::move(simulation)) {
	ZoneScoped;
	static_assert(sizeof(PushConstants) == 32, "PushConstants is mirrored by debris_draw.slang");

	const auto uid = assets::resolveURI("core://shaders/debris_draw.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "DebrisPass shader core://shaders/debris_draw.slang unavailable; debris will not draw");
		setEnabled(false);
		return;
	}

	m_layout.rebuild(core, shader->reflection, "DebrisPass");
	const auto& layouts = m_layout.getDescriptorSetLayouts();
	if (layouts.size() <= k_debris_set) {
		TOAST_ERROR(
		    "Render", "DebrisPass shader layout declares {} descriptor sets; it needs the scene and debris sets", layouts.size()
		);
		setEnabled(false);
		return;
	}

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "DebrisPass";
	config.color_format = k_voxel_albedo_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.extra_color_formats = voxelGbufferExtraColorFormats();
	config.write_extra_color = true;
	config.write_color_alpha = true;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_layout.getPipelineLayout();
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.cull_mode = vk::CullModeFlagBits::eBack;
	config.depth_test = true;
	config.depth_write = true;
	m_pipeline.rebuild(core, config);

	// Same targets and blending as VoxelPass glass
	const bool dual_source = core.isDualSourceBlendSupported();
	config.debug_name = "DebrisPass Glass";
	config.color_format = scene_format;
	config.extra_color_formats = worldStageExtraColorFormats();
	config.write_extra_color = false;
	config.write_color_alpha = false;
	config.vertex_entry = "vertexGlass";
	config.fragment_entry = dual_source ? "fragmentGlass" : "fragmentGlassPremultiplied";
	config.depth_write = false;
	config.blend_preset = dual_source ? VulkanPipeline::BlendPreset::dual_source : VulkanPipeline::BlendPreset::premultiplied;
	m_glass_pipeline.rebuild(core, config);

	m_scene_sets.create(core, shader->reflection, *layouts[k_debris_scene_set], "DebrisPass");

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout set_layout = *layouts[k_debris_set];
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		auto sets = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &set_layout));
		m_sets.push_back(std::move(sets[0]));
		setDebugName(core, *m_sets.back(), std::format("DebrisPass Set[{}]", i));
	}
	m_bound.resize(VulkanRenderer::k_frames_in_flight);

	// Own set so the shadow pass can keep binding set 1 with its own layout
	if (layouts.size() > k_debris_backdrop_set) {
		m_backdrop_sampler = vk::raii::Sampler(core.getDevice(), linearClampSamplerInfo());
		setDebugName(core, *m_backdrop_sampler, "DebrisPass BackdropSampler");
		m_backdrop_depth_sampler = vk::raii::Sampler(core.getDevice(), nearestClampSamplerInfo());
		setDebugName(core, *m_backdrop_depth_sampler, "DebrisPass BackdropDepthSampler");
		const vk::DescriptorSetLayout backdrop_layout = *layouts[k_debris_backdrop_set];
		for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
			auto sets = core.getDevice().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(pool, 1, &backdrop_layout));
			m_backdrop_sets.push_back(std::move(sets[0]));
			setDebugName(core, *m_backdrop_sets.back(), std::format("DebrisPass BackdropSet[{}]", i));
		}
		m_bound_backdrop.resize(VulkanRenderer::k_frames_in_flight);
	}
}

auto DebrisPass::prepare(vk::CommandBuffer cmd, uint32_t frame_index) -> bool {
	ZoneScoped;
	m_args = nullptr;
	if (!m_pipeline.isReady() || m_simulation == nullptr || !m_simulation->isReady() || frame_index >= m_sets.size() ||
	    !m_scene_sets.get(frame_index)) {
		return false;
	}

	const auto view = m_simulation->acquireForFrame(cmd, frame_index, sync::Usage::vertex_storage_read);
	if (!view || !m_simulation->visible()) {
		return false;
	}

	const vk::Buffer particles = view.buffer(m_simulation->particlesId());
	const vk::Buffer list = view.buffer(m_simulation->drawListId());
	const vk::Buffer args = view.buffer(m_simulation->drawArgsId());
	if (!particles || !list || !args) {
		return false;
	}

	if (m_bound.update(frame_index, std::array {particles, list})) {
		const vk::DescriptorBufferInfo particle_info(particles, 0, VK_WHOLE_SIZE);
		const vk::DescriptorBufferInfo list_info(list, 0, VK_WHOLE_SIZE);
		const std::array writes {
		  vk::WriteDescriptorSet(
		      *m_sets[frame_index], k_debris_particles_binding, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &particle_info
		  ),
		  vk::WriteDescriptorSet(
		      *m_sets[frame_index], k_debris_list_binding, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &list_info
		  ),
		};
		m_core->getDevice().updateDescriptorSets(writes, {});
	}

	m_scene_sets.updateTlas(frame_index);
	m_args = args;
	m_push = PushConstants {
	  .extrapolation = m_simulation->extrapolation(view.step()),
	  .frame_delta = m_simulation->frameDelta(),
	  .voxel_size = voxel::k_voxel_size,
	  .list_capacity = m_simulation->capacity() * physics::DebrisSimulation::k_voxels_per_particle,
	};
	m_shadow = shadowBoundsOf(*m_simulation, view, m_push.extrapolation);
	m_shadow.extrapolation = m_push.extrapolation;
	m_shadow.voxel_size = m_push.voxel_size;
	m_shadow.list_capacity = m_push.list_capacity;
	return true;
}

namespace {

auto shadowBoundsOf(const physics::DebrisSimulation& simulation, const AsyncComputeTask::FrameView& view, float extrapolation)
    -> DebrisPass::ShadowDraw {
	DebrisPass::ShadowDraw draw;
	physics::DebrisSimulation::ShadowBounds bounds;
	if (!view.read(simulation.shadowBoundsId(), std::as_writable_bytes(std::span {&bounds, 1}))) {
		// No readback so cast everywhere and redraw every frame
		draw.bounds_radius = std::numeric_limits<float>::max();
		return draw;
	}

	const auto unordered = [](uint32_t bits) {
		return std::bit_cast<float>((bits & 0x80000000u) != 0u ? bits & 0x7FFFFFFFu : ~bits);
	};
	const glm::vec3 low(unordered(bounds.min[0]), unordered(bounds.min[1]), unordered(bounds.min[2]));
	const glm::vec3 high(unordered(bounds.max[0]), unordered(bounds.max[1]), unordered(bounds.max[2]));
	if (bounds.min[0] > bounds.max[0]) {
		// No opaque pieces, stable so their last shadow clears once
		draw.signature = 0;
		return draw;
	}

	// The step is drawn moved by velocity times the extrapolation
	const float drift = std::bit_cast<float>(bounds.max_speed) * std::abs(extrapolation);
	draw.bounds_center = (low + high) * 0.5f;
	draw.bounds_radius = (glm::length(high - low) * 0.5f) + drift;
	if (bounds.changing == 0) {
		uint64_t hash = 0xCBF29CE484222325ull;
		for (const uint32_t word : {bounds.min[0], bounds.min[1], bounds.min[2], bounds.max[0], bounds.max[1], bounds.max[2]}) {
			hash = (hash ^ word) * 0x100000001B3ull;
		}
		draw.signature = hash;
	}
	return draw;
}

}

void DebrisPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;
	if (!m_args || frame_index >= m_sets.size()) {
		return;
	}

	const vk::PipelineLayout layout = *m_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_debris_scene_set,
	    std::array<vk::DescriptorSet, 2> {m_scene_sets.get(frame_index), *m_sets[frame_index]},
	    {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &m_push);
	cmd.drawIndirect(m_args, 0, 1, sizeof(vk::DrawIndirectCommand));
}

void DebrisPass::recordGlass(vk::CommandBuffer cmd, uint32_t frame_index) {
	ZoneScoped;
	// Cleared here since prepare only runs on frames with a voxel scope
	const vk::Buffer args = std::exchange(m_args, nullptr);
	if (!args || !m_glass_pipeline.isReady() || frame_index >= m_sets.size() || frame_index >= m_backdrop_sets.size()) {
		return;
	}

	auto* renderer = VulkanRenderer::instance;
	const std::array<vk::ImageView, 2> backdrop {renderer->getGlassBackdropView(), renderer->getGlassBackdropDepthView()};
	if (!backdrop[0] || !backdrop[1]) {
		return;
	}
	// The set is first bound below so updating it here is safe
	if (m_bound_backdrop.update(frame_index, backdrop)) {
		DescriptorWriter writer;
		writer.image(
		    *m_backdrop_sets[frame_index],
		    k_debris_backdrop_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_backdrop_sampler,
		    backdrop[0],
		    vk::ImageLayout::eShaderReadOnlyOptimal
		);
		writer.image(
		    *m_backdrop_sets[frame_index],
		    k_debris_backdrop_depth_binding,
		    vk::DescriptorType::eCombinedImageSampler,
		    *m_backdrop_depth_sampler,
		    backdrop[1],
		    vk::ImageLayout::eDepthReadOnlyOptimal
		);
		writer.flush(m_core->getDevice());
	}

	PushConstants push = m_push;
	// Traced frames too since otherwise glass debris keeps the bare environment while the voxels around it reflect the scene
	if ((renderer->ssrFrame() || renderer->tracedSpecularFrame()) && renderer->glassBackdropCaptured()) {
		const auto ssr = renderer->glassSsrSettings();
		push.reflection_reach = ssr.stride * static_cast<float>(ssr.max_steps);
		push.reflection_steps = ssr.max_steps;
		push.reflection_thickness = ssr.thickness;
		push.reflection_roughness = ssr.max_roughness;
	}

	const vk::PipelineLayout layout = *m_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_glass_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_debris_scene_set,
	    std::array<vk::DescriptorSet, 3> {m_scene_sets.get(frame_index), *m_sets[frame_index], *m_backdrop_sets[frame_index]},
	    {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &push);
	cmd.drawIndirect(args, sizeof(vk::DrawIndirectCommand), 1, sizeof(vk::DrawIndirectCommand));
}

}
