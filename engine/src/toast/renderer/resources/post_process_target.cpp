/**
 * @file post_process_target.cpp
 * @author dario
 * @date 14/08/2026
 */

#include "toast/renderer/resources/post_process_target.hpp"

#include <mutex>
#include <unordered_map>

namespace renderer {

namespace {

std::mutex g_post_target_images_mutex;
std::unordered_map<VkImageView, VkImage> g_post_target_images;

}

auto PostProcessTarget::imageOf(vk::ImageView view) -> vk::Image {
	const std::lock_guard lock(g_post_target_images_mutex);
	const auto it = g_post_target_images.find(view);
	return it != g_post_target_images.end() ? vk::Image(it->second) : vk::Image {};
}

PostProcessTarget::~PostProcessTarget() {
	if (m_view.has_value()) {
		const std::lock_guard lock(g_post_target_images_mutex);
		g_post_target_images.erase(**m_view);
	}
}

void PostProcessTarget::create(
    const VulkanCore& core, vk::Extent2D extent, vk::Format format, std::string_view debug_name, vk::ImageUsageFlags usage
) {
	ZoneScoped;
	if (m_view.has_value()) {
		const std::lock_guard lock(g_post_target_images_mutex);
		g_post_target_images.erase(**m_view);
	}
	// View before the image it views
	m_view.reset();
	m_image.reset();
	m_extent = extent;
	m_state = {};

	const auto image_ci = colorTargetImageInfo(extent, format, usage);

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_image.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(core, **m_image, std::format("{} Target", debug_name));
	m_state.reset(**m_image, colorSubresourceRange());

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = format;
	view_ci.subresourceRange = colorSubresourceRange();
	m_view.emplace(core.getDevice(), view_ci);
	setDebugName(core, **m_view, std::format("{} TargetView", debug_name));

	const std::lock_guard lock(g_post_target_images_mutex);
	g_post_target_images[**m_view] = **m_image;
}

void PostProcessTarget::beginScope(vk::CommandBuffer cmd) const {
	if (!isReady()) {
		return;
	}

	sync::transition(cmd, m_state, sync::Usage::color_attachment);

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

	// Auto exposure reads it from compute too
	sync::transition(cmd, m_state, sync::Usage::shader_sampled);
}

void PostProcessTarget::makeSampleable(vk::CommandBuffer cmd) const {
	if (isReady()) {
		sync::transition(cmd, m_state, sync::Usage::shader_sampled);
	}
}

}
