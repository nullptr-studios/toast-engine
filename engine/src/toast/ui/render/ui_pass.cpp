#include "ui_pass.hpp"

#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/renderer/shader_cache.hpp>
#include <toast/renderer/vulkan_core.hpp>
#include <toast/renderer/vulkan_debug.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <tracy/Tracy.hpp>

namespace ui {

auto UIPass::selectStencilFormat(const renderer::VulkanCore& core) -> vk::Format {
	constexpr std::array candidates {
	  vk::Format::eS8Uint,
	  vk::Format::eD24UnormS8Uint,
	  vk::Format::eD32SfloatS8Uint,
	};

	for (const auto format : candidates) {
		const auto props = core.getPhysicalDevice().getFormatProperties(format);
		if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
			return format;
		}
	}

	TOAST_CRITICAL("UI", "No stencil-capable format supported; UI clip masks require one");
	return vk::Format::eUndefined;
}

UIPass::UIPass(
    const renderer::VulkanCore& core, vk::Format output_color_format, vk::Format output_depth_format, vk::Extent2D extent
)
    : m_core(&core) {
	const auto uid = assets::resolveURI("core://shaders/ui.slang");
	const auto shader = uid.has_value() ? renderer::ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("UI", "UIPass shader core://shaders/ui.slang unavailable, the UI will not draw");
		return;
	}

	m_shader_layout.rebuild(core, shader->reflection, "ui_composite");

	renderer::VulkanPipeline::Config config;
	config.pipeline_type = renderer::VulkanPipeline::PipelineType::graphics;
	config.debug_name = "UIPass Composite";
	config.color_format = output_color_format;
	// The composite draws inside the renderer's main scope, whose depth attachment format the
	// pipeline must declare even with depth testing off
	config.depth_format = output_depth_format;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	config.blend_preset = renderer::VulkanPipeline::BlendPreset::premultiplied;
	m_composite_pipeline.rebuild(core, config);

	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = vk::Filter::eLinear;
	sampler_ci.minFilter = vk::Filter::eLinear;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
	m_sampler = vk::raii::Sampler(core.getDevice(), sampler_ci);

	TOAST_INFO("UI", "UI pass ready ({}x{})", extent.width, extent.height);
}

void UIPass::recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)image_index;
	ZoneScopedN("UIPass::recordPre");

	m_draw_count = 0;

	const auto* frame = renderer::VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->ui_command_buffers.empty()) {
		return;
	}

	// The renderer waited this frame context's fence before recording, so whatever guard was
	// stored here previously is no longer executing on the GPU
	m_executing_guards[frame_index % m_executing_guards.size()] = frame->ui_slot_guard;

	// The secondary buffers open their own rendering scopes, so they execute outside the main one
	cmd.executeCommands(frame->ui_command_buffers);

	auto& sets = m_frame_sets[frame_index % m_frame_sets.size()];
	const auto& device = m_core->getDevice();
	const vk::DescriptorSetLayout set_layout = *m_shader_layout.getDescriptorSetLayouts()[0];

	for (const vk::ImageView view : frame->ui_output_views) {
		if (!view) {
			continue;
		}

		if (m_draw_count == sets.size()) {
			const vk::DescriptorSetAllocateInfo alloc_info(
			    renderer::VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &set_layout
			);
			auto allocated = device.allocateDescriptorSets(alloc_info);
			sets.push_back(std::move(allocated[0]));
		}

		const vk::DescriptorImageInfo image_info(*m_sampler, view, vk::ImageLayout::eShaderReadOnlyOptimal);
		const vk::WriteDescriptorSet write(*sets[m_draw_count], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info);
		device.updateDescriptorSets(write, {});
		m_draw_count++;
	}
}

void UIPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)image_index;

	if (m_draw_count == 0 || !m_composite_pipeline.isReady()) {
		return;
	}

	ZoneScopedN("UIPass::record");

	auto& sets = m_frame_sets[frame_index % m_frame_sets.size()];

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_composite_pipeline.getPipeline());
	for (uint32_t i = 0; i < m_draw_count; i++) {
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_shader_layout.getPipelineLayout(), 0, *sets[i], nullptr);
		cmd.draw(3, 1, 0, 0);
	}
}

}
