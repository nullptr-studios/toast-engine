#include "world_ui_pass.hpp"

#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/renderer/shader_cache.hpp>
#include <toast/renderer/vulkan_core.hpp>
#include <toast/renderer/vulkan_debug.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <tracy/Tracy.hpp>

namespace ui {

WorldUIPass::WorldUIPass(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent)
    : m_core(&core) {
	const auto uid = assets::resolveURI("core://shaders/ui_world.slang");
	const auto shader = uid.has_value() ? renderer::ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("UI", "WorldUIPass shader core://shaders/ui_world.slang unavailable, world UI panels will not draw");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "ui_world");

	renderer::VulkanPipeline::Config config;
	config.pipeline_type = renderer::VulkanPipeline::PipelineType::graphics;
	config.debug_name = "WorldUIPass";
	config.color_format = color_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = true;
	config.depth_write = true;
	config.blend_preset = renderer::VulkanPipeline::BlendPreset::premultiplied;
	m_pipeline.rebuild(core, config);

	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = vk::Filter::eLinear;
	sampler_ci.minFilter = vk::Filter::eLinear;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
	m_sampler = vk::raii::Sampler(core.getDevice(), sampler_ci);

	// Per-frame camera UBO sets
	const auto& device = core.getDevice();
	const vk::DescriptorSetLayout camera_layout = *m_shader_layout.getDescriptorSetLayouts()[0];
	const vk::DescriptorPool pool = renderer::VulkanRenderer::instance->getDescriptorPoolHandle();

	m_frame_camera_sets.reserve(renderer::VulkanRenderer::k_frames_in_flight);
	for (uint32_t i = 0; i < renderer::VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &camera_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_frame_camera_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_frame_camera_sets[i], std::format("WorldUIPass CameraSet[{}]", i));

		const auto* frame_res = renderer::VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			TOAST_CRITICAL("UI", "Frame UBO buffer missing for frame {}", i);
			continue;
		}

		const vk::DescriptorBufferInfo buffer_info(**frame_res->gpu_buffer, 0, sizeof(renderer::VulkanRenderer::FrameUBO));
		const vk::WriteDescriptorSet write(
		    *m_frame_camera_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info
		);
		device.updateDescriptorSets(write, {});
	}

	TOAST_INFO("UI", "World UI pass ready");
}

void WorldUIPass::recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)cmd;
	(void)image_index;
	ZoneScopedN("WorldUIPass::recordPre");

	m_draw_count = 0;

	const auto* frame = renderer::VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->ui_world_panels.empty()) {
		return;
	}

	// Panel textures were rendered by UIPass::recordPre
	auto& sets = m_panel_sets[frame_index % m_panel_sets.size()];
	const auto& device = m_core->getDevice();
	const vk::DescriptorSetLayout texture_layout = *m_shader_layout.getDescriptorSetLayouts()[1];

	for (const auto& panel : frame->ui_world_panels) {
		if (!panel.view) {
			continue;
		}

		if (m_draw_count == sets.size()) {
			const vk::DescriptorSetAllocateInfo alloc_info(
			    renderer::VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &texture_layout
			);
			auto allocated = device.allocateDescriptorSets(alloc_info);
			sets.push_back(std::move(allocated[0]));
		}

		const vk::DescriptorImageInfo image_info(*m_sampler, panel.view, vk::ImageLayout::eShaderReadOnlyOptimal);
		const vk::WriteDescriptorSet write(*sets[m_draw_count], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info);
		device.updateDescriptorSets(write, {});
		m_draw_count++;
	}
}

void WorldUIPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)image_index;

	if (m_draw_count == 0 || !m_pipeline.isReady()) {
		return;
	}

	ZoneScopedN("WorldUIPass::record");

	const auto* frame = renderer::VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	auto& sets = m_panel_sets[frame_index % m_panel_sets.size()];
	const vk::PipelineLayout layout = *m_shader_layout.getPipelineLayout();

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline.getPipeline());
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics, layout, 0, *m_frame_camera_sets[frame_index % m_frame_camera_sets.size()], nullptr
	);

	uint32_t draw_index = 0;
	for (const auto& panel : frame->ui_world_panels) {
		if (!panel.view) {
			continue;
		}

		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, *sets[draw_index], nullptr);

		const PushConstants push {panel.model};
		cmd.pushConstants<PushConstants>(layout, vk::ShaderStageFlagBits::eVertex, 0, push);
		cmd.draw(6, 1, 0, 0);
		draw_index++;
	}
}

}
