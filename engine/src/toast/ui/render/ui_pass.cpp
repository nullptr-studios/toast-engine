#include "ui_pass.hpp"

#include <toast/log.hpp>
#include <toast/renderer/shader_compiler.hpp>
#include <toast/renderer/vulkan_core.hpp>
#include <toast/renderer/vulkan_debug.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <tracy/Tracy.hpp>

namespace ui {

namespace {

auto stencilAspect(vk::Format format) -> vk::ImageAspectFlags {
	if (format == vk::Format::eS8Uint) {
		return vk::ImageAspectFlagBits::eStencil;
	}
	return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
}

}

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
    : m_core(&core),
      m_stencil_format(selectStencilFormat(core)) {
	m_shader_layout.rebuild(core, "ui_composite");

	auto shader = renderer::ShaderCompiler::compileShaderModuleFromSource("./ui.slang");

	renderer::VulkanPipeline::Config config;
	config.pipeline_type = renderer::VulkanPipeline::PipelineType::graphics;
	config.debug_name = "UIPass Composite";
	config.color_format = output_color_format;
	// The composite draws inside the renderer's main scope, whose depth attachment format the
	// pipeline must declare even with depth testing off
	config.depth_format = output_depth_format;
	config.extent = extent;
	config.shader_spirv = std::move(shader.spirv);
	config.pipeline_layout = *m_shader_layout.getPipelineLayout();
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = false;
	config.depth_write = false;
	config.blend_enable = true;
	config.premultiplied_blend = true;
	m_composite_pipeline.rebuild(core, config);

	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = vk::Filter::eLinear;
	sampler_ci.minFilter = vk::Filter::eLinear;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
	m_sampler = vk::raii::Sampler(core.getDevice(), sampler_ci);

	const vk::DescriptorSetLayout set_layout = *m_shader_layout.getDescriptorSetLayouts()[0];
	const vk::DescriptorSetAllocateInfo alloc_info(renderer::VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &set_layout);
	auto allocated = core.getDevice().allocateDescriptorSets(alloc_info);
	m_descriptor_set = std::move(allocated[0]);
	setDebugName(core, *m_descriptor_set, "UIPass CompositeSet");

	createTarget(extent);

	TOAST_INFO("UI", "UI pass ready ({}x{}, stencil {})", extent.width, extent.height, vk::to_string(m_stencil_format));
}

void UIPass::createTarget(vk::Extent2D extent) {
	const auto& device = m_core->getDevice();

	Target target;
	target.extent = extent;

	vk::ImageCreateInfo color_ci {};
	color_ci.imageType = vk::ImageType::e2D;
	color_ci.format = k_color_format;
	color_ci.extent = vk::Extent3D {extent.width, extent.height, 1};
	color_ci.mipLevels = 1;
	color_ci.arrayLayers = 1;
	color_ci.samples = vk::SampleCountFlagBits::e1;
	color_ci.tiling = vk::ImageTiling::eOptimal;
	color_ci.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
	color_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	target.color_image.emplace(m_core->getAllocator().createImage(color_ci, allocation_ci));
	setDebugName(*m_core, **target.color_image, "UIPass ColorImage");

	vk::ImageViewCreateInfo color_view_ci {};
	color_view_ci.image = **target.color_image;
	color_view_ci.viewType = vk::ImageViewType::e2D;
	color_view_ci.format = k_color_format;
	color_view_ci.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
	target.color_view = vk::raii::ImageView(device, color_view_ci);
	setDebugName(*m_core, *target.color_view, "UIPass ColorView");

	vk::ImageCreateInfo stencil_ci = color_ci;
	stencil_ci.format = m_stencil_format;
	stencil_ci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;

	target.stencil_image.emplace(m_core->getAllocator().createImage(stencil_ci, allocation_ci));
	setDebugName(*m_core, **target.stencil_image, "UIPass StencilImage");

	vk::ImageViewCreateInfo stencil_view_ci {};
	stencil_view_ci.image = **target.stencil_image;
	stencil_view_ci.viewType = vk::ImageViewType::e2D;
	stencil_view_ci.format = m_stencil_format;
	stencil_view_ci.subresourceRange = vk::ImageSubresourceRange(stencilAspect(m_stencil_format), 0, 1, 0, 1);
	target.stencil_view = vk::raii::ImageView(device, stencil_view_ci);
	setDebugName(*m_core, *target.stencil_view, "UIPass StencilView");

	if (m_target.color_image.has_value()) {
		// The previous target might still be sampled by frames in flight
		m_retired_targets.push_back({std::move(m_target), renderer::VulkanRenderer::k_frames_in_flight + 1});
	}
	m_target = std::move(target);
	m_has_content = false;

	writeDescriptor();
}

void UIPass::writeDescriptor() {
	const vk::DescriptorImageInfo image_info(*m_sampler, *m_target.color_view, vk::ImageLayout::eShaderReadOnlyOptimal);
	const vk::WriteDescriptorSet write(*m_descriptor_set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info);
	m_core->getDevice().updateDescriptorSets(write, {});
}

void UIPass::recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)image_index;
	ZoneScopedN("UIPass::recordPre");

	for (auto it = m_retired_targets.begin(); it != m_retired_targets.end();) {
		it->frames_left--;
		it = it->frames_left == 0 ? m_retired_targets.erase(it) : std::next(it);
	}

	const auto* frame = renderer::VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame->ui_command_buffers.empty()) {
		m_has_content = false;
		return;
	}

	// The renderer waited this frame context's fence before recording, so whatever guard was
	// stored here previously is no longer executing on the GPU
	m_executing_guards[frame_index % m_executing_guards.size()] = frame->ui_slot_guard;

	// The output target extent is the viewport size; recreate the UI layer when it changes
	const auto extent = renderer::VulkanRenderer::instance->getOutputTarget().getExtent();
	if (extent.width != m_target.extent.width || extent.height != m_target.extent.height) {
		if (extent.width == 0 || extent.height == 0) {
			m_has_content = false;
			return;
		}
		createTarget(extent);
	}

	const vk::ImageSubresourceRange color_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

	// undefined → color attachment; contents are cleared every frame so the previous layout
	// never matters
	vk::ImageMemoryBarrier to_attachment {};
	to_attachment.oldLayout = vk::ImageLayout::eUndefined;
	to_attachment.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
	to_attachment.srcAccessMask = {};
	to_attachment.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
	to_attachment.image = **m_target.color_image;
	to_attachment.subresourceRange = color_range;
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eFragmentShader,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    {},
	    nullptr,
	    nullptr,
	    to_attachment
	);

	vk::ImageMemoryBarrier stencil_to_attachment {};
	stencil_to_attachment.oldLayout = vk::ImageLayout::eUndefined;
	stencil_to_attachment.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	stencil_to_attachment.srcAccessMask = {};
	stencil_to_attachment.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	stencil_to_attachment.image = **m_target.stencil_image;
	stencil_to_attachment.subresourceRange = vk::ImageSubresourceRange(stencilAspect(m_stencil_format), 0, 1, 0, 1);
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTopOfPipe,
	    vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
	    {},
	    nullptr,
	    nullptr,
	    stencil_to_attachment
	);

	vk::RenderingAttachmentInfo color_attachment {};
	color_attachment.imageView = *m_target.color_view;
	color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
	color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
	color_attachment.clearValue = vk::ClearValue(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 0.0f}));

	vk::RenderingAttachmentInfo stencil_attachment {};
	stencil_attachment.imageView = *m_target.stencil_view;
	stencil_attachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	stencil_attachment.loadOp = vk::AttachmentLoadOp::eClear;
	stencil_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
	stencil_attachment.clearValue = vk::ClearValue(vk::ClearDepthStencilValue {1.0f, 0});

	vk::RenderingInfo rendering_info {};
	rendering_info.flags = vk::RenderingFlagBits::eContentsSecondaryCommandBuffers;
	rendering_info.renderArea = vk::Rect2D({0, 0}, m_target.extent);
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment;
	if (m_stencil_format != vk::Format::eS8Uint) {
		rendering_info.pDepthAttachment = &stencil_attachment;
	}
	rendering_info.pStencilAttachment = &stencil_attachment;

	cmd.beginRendering(rendering_info);
	cmd.executeCommands(frame->ui_command_buffers);
	cmd.endRendering();

	vk::ImageMemoryBarrier to_sampled {};
	to_sampled.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
	to_sampled.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	to_sampled.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
	to_sampled.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	to_sampled.image = **m_target.color_image;
	to_sampled.subresourceRange = color_range;
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    vk::PipelineStageFlagBits::eFragmentShader,
	    {},
	    nullptr,
	    nullptr,
	    to_sampled
	);

	m_has_content = true;
}

void UIPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)frame_index;
	(void)image_index;

	if (!m_has_content || !m_composite_pipeline.isReady()) {
		return;
	}

	ZoneScopedN("UIPass::record");

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_composite_pipeline.getPipeline());
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_shader_layout.getPipelineLayout(), 0, *m_descriptor_set, nullptr);
	cmd.draw(3, 1, 0, 0);
}

}
