#include "debris_pass.hpp"

#include "../gpu_sync.hpp"
#include "../shader_cache.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"

#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/physics/debris_simulation.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace renderer {

namespace {

constexpr uint32_t k_debris_scene_set = 0;
constexpr uint32_t k_debris_set = 1;
constexpr uint32_t k_debris_particles_binding = 0;
constexpr uint32_t k_debris_list_binding = 1;

}

DebrisPass::DebrisPass(
    const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent,
    std::shared_ptr<physics::DebrisSimulation> simulation
)
    : m_core(&core),
      m_simulation(std::move(simulation)) {
	ZoneScoped;
	static_assert(sizeof(PushConstants) == 16, "PushConstants is mirrored by debris_draw.slang");

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
	m_bound.assign(VulkanRenderer::k_frames_in_flight, {});
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

	if (m_bound[frame_index] != std::array {particles, list}) {
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
		m_bound[frame_index] = {particles, list};
	}

	m_scene_sets.updateTlas(frame_index);
	m_args = args;
	m_push = PushConstants {
	  .extrapolation = m_simulation->extrapolation(view.step()),
	  .frame_delta = m_simulation->frameDelta(),
	  .voxel_size = voxel::k_voxel_size,
	  .list_capacity = m_simulation->capacity() * physics::DebrisSimulation::k_voxels_per_particle,
	};
	return true;
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
	if (!args || !m_glass_pipeline.isReady() || frame_index >= m_sets.size()) {
		return;
	}

	const vk::PipelineLayout layout = *m_layout.getPipelineLayout();
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_glass_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    layout,
	    k_debris_scene_set,
	    std::array<vk::DescriptorSet, 2> {m_scene_sets.get(frame_index), *m_sets[frame_index]},
	    {}
	);
	cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstants), &m_push);
	cmd.drawIndirect(args, sizeof(vk::DrawIndirectCommand), 1, sizeof(vk::DrawIndirectCommand));
}

}
