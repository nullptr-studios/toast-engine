/**
 * @file post_process_target.cpp
 * @author dario
 * @date 14/08/2026
 */

#include "post_process_target.hpp"

#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <array>
#include <format>
#include <tracy/Tracy.hpp>

namespace renderer {

void PostProcessTarget::create(const VulkanCore& core, vk::Extent2D extent, vk::Format format, std::string_view debug_name) {
	ZoneScoped;
	// View before the image it views
	m_view.reset();
	m_image.reset();
	m_extent = extent;
	m_layout = vk::ImageLayout::eUndefined;

	const auto image_ci = colorTargetImageInfo(extent, format);

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_image.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(core, **m_image, std::format("{} Target", debug_name));

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = format;
	view_ci.subresourceRange = colorSubresourceRange();
	m_view.emplace(core.getDevice(), view_ci);
	setDebugName(core, **m_view, std::format("{} TargetView", debug_name));
}

void PostProcessTarget::beginScope(vk::CommandBuffer cmd) const {
	if (!isReady()) {
		return;
	}

	const bool was_sampled = m_layout == vk::ImageLayout::eShaderReadOnlyOptimal;

	const vk::ImageMemoryBarrier to_attachment(
	    was_sampled ? vk::AccessFlagBits::eShaderRead : vk::AccessFlags {},
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    m_layout,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    **m_image,
	    colorSubresourceRange()
	);
	cmd.pipelineBarrier(
	    was_sampled ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eTopOfPipe,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    {},
	    nullptr,
	    nullptr,
	    to_attachment
	);
	m_layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::RenderingAttachmentInfo attachment {};
	attachment.imageView = **m_view;
	attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
	attachment.storeOp = vk::AttachmentStoreOp::eStore;

	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, m_extent);
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &attachment;

	cmd.beginRendering(rendering_info);

	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f);
	cmd.setViewport(0, std::array {viewport});
	cmd.setScissor(0, std::array {vk::Rect2D({0, 0}, m_extent)});
}

void PostProcessTarget::endScope(vk::CommandBuffer cmd) const {
	if (!isReady()) {
		return;
	}

	cmd.endRendering();

	const vk::ImageMemoryBarrier to_sampled(
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::AccessFlagBits::eShaderRead,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    vk::ImageLayout::eShaderReadOnlyOptimal,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    **m_image,
	    colorSubresourceRange()
	);
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    vk::PipelineStageFlagBits::eFragmentShader,
	    {},
	    nullptr,
	    nullptr,
	    to_sampled
	);
	m_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

}
