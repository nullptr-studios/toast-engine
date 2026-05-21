/// @file VulkanRenderer.cpp
/// @author dario
/// @date 16/05/2026.

#include "VulkanRenderer.hpp"

#include "toast/log.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace toast::renderer {

namespace {
auto colorAttachmentRange() -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
}

auto transitionImageLayout(
    vk::CommandBuffer command_buffer, vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
    vk::AccessFlags src_access_mask, vk::AccessFlags dst_access_mask, vk::PipelineStageFlags src_stage_mask,
    vk::PipelineStageFlags dst_stage_mask
) -> void {
	const vk::ImageMemoryBarrier barrier(
	    src_access_mask,
	    dst_access_mask,
	    old_layout,
	    new_layout,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    image,
	    colorAttachmentRange()
	);
	command_buffer.pipelineBarrier(src_stage_mask, dst_stage_mask, {}, {}, {}, barrier);
}
}    // namespace

VulkanRenderer::VulkanRenderer(
    const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target, std::unique_ptr<VulkanPipeline> pipeline,
    uint32_t frames_in_flight
)
    : m_core(&core),
      m_outputTarget(std::move(output_target)),
      m_pipeline(std::move(pipeline)) {
	if (!m_outputTarget) {
		throw std::runtime_error("Toast Engine Error: VulkanRenderer requires an output target!");
	}

	if (!m_pipeline || !m_pipeline->isReady()) {
		throw std::runtime_error("Toast Engine Error: VulkanRenderer requires a valid pipeline!");
	}
	if (frames_in_flight == 0) {
		throw std::runtime_error("Toast Engine Error: VulkanRenderer requires at least one frame in flight!");
	}

	TOAST_INFO("VulkanRenderer", "Creating renderer with {} frame(s) in flight", frames_in_flight);

	createCommandPool();
	createFrameContexts(frames_in_flight);
	createPerImageSync();
	m_imagesInFlight.assign(m_outputTarget->getImageCount(), vk::Fence {});
	m_imageLayouts.assign(m_outputTarget->getImageCount(), vk::ImageLayout::eUndefined);
}

auto VulkanRenderer::createCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getGraphicsQueueFamilyIndex()
	);
	m_commandPool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	TOAST_INFO("VulkanRenderer", "Command pool created (graphics family {})", m_core->getGraphicsQueueFamilyIndex());
}

auto VulkanRenderer::createFrameContexts(uint32_t frames_in_flight) -> void {
	m_frames.clear();
	m_frames.resize(frames_in_flight);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	const vk::FenceCreateInfo fence_ci(vk::FenceCreateFlagBits::eSignaled);
	const vk::CommandBufferAllocateInfo command_buffer_ci(*m_commandPool, vk::CommandBufferLevel::ePrimary, frames_in_flight);
	auto allocated_command_buffers = m_core->getDevice().allocateCommandBuffers(command_buffer_ci);

	for (uint32_t frame_index = 0; frame_index < frames_in_flight; ++frame_index) {
		m_frames[frame_index].commandBuffer = std::move(allocated_command_buffers[frame_index]);
		m_frames[frame_index].imageAvailable = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].inFlight = vk::raii::Fence(m_core->getDevice(), fence_ci);
	}

	TOAST_INFO("VulkanRenderer", "Frame command buffers created: {}", frames_in_flight);
}

auto VulkanRenderer::createPerImageSync() -> void {
	const auto image_count = m_outputTarget->getImageCount();
	m_renderFinishedPerImage.clear();
	m_renderFinishedPerImage.reserve(image_count);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	for (uint32_t i = 0; i < image_count; ++i) {
		m_renderFinishedPerImage.emplace_back(m_core->getDevice(), semaphore_ci);
	}

	TOAST_INFO("VulkanRenderer", "Per-image semaphores created: {}", image_count);
}

auto VulkanRenderer::recordFrame(FrameContext& frame, uint32_t image_index) -> void {
	frame.commandBuffer.reset();
	const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	frame.commandBuffer.begin(begin_info);

	const vk::Image image = m_outputTarget->getColorImage(image_index);
	const vk::ImageLayout old_layout = m_imageLayouts.at(image_index);
	const vk::ImageLayout color_attachment_layout = vk::ImageLayout::eColorAttachmentOptimal;

	if (old_layout != color_attachment_layout) {
		transitionImageLayout(
		    frame.commandBuffer,
		    image,
		    old_layout,
		    color_attachment_layout,
		    vk::AccessFlags {},
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::PipelineStageFlagBits::eTopOfPipe,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput
		);
	}

	const vk::ClearValue clear_color(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 1.0f}));
	vk::RenderingAttachmentInfo color_attachment_info {};
	color_attachment_info.imageView = *m_outputTarget->getColorAttachment(image_index);
	color_attachment_info.imageLayout = color_attachment_layout;
	color_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	color_attachment_info.resolveImageView = nullptr;
	color_attachment_info.resolveImageLayout = vk::ImageLayout::eUndefined;
	color_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	color_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	color_attachment_info.clearValue = clear_color;

	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, m_outputTarget->getExtent());
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment_info;
	frame.commandBuffer.beginRendering(rendering_info);

	// Bind pipeline
	frame.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline->getPipeline());

	// Draw triangle
	frame.commandBuffer.draw(6, 1, 0, 0);

	// End rendering
	frame.commandBuffer.endRendering();

	const vk::ImageLayout present_layout = vk::ImageLayout::ePresentSrcKHR;
	transitionImageLayout(
	    frame.commandBuffer,
	    image,
	    color_attachment_layout,
	    present_layout,
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::AccessFlags {},
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    vk::PipelineStageFlagBits::eBottomOfPipe
	);

	m_imageLayouts[image_index] = present_layout;

	frame.commandBuffer.end();
}

auto VulkanRenderer::drawFrame() -> void {
	if (m_frames.empty()) {
		return;
	}

	auto& frame = m_frames[m_currentFrame];
	static_cast<void>(m_core->getDevice().waitForFences(*frame.inFlight, true, std::numeric_limits<uint64_t>::max()));

	const auto acquired =
	    m_outputTarget->acquireNextImage(std::numeric_limits<uint64_t>::max(), *frame.imageAvailable, VK_NULL_HANDLE);

	if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
		TOAST_WARN("VulkanRenderer", "Swapchain out of date on acquire; skipping frame");
		return;
	}
	if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR) {
		throw std::runtime_error("Toast Engine Error: Failed to acquire the next output image!");
	}

	const uint32_t image_index = acquired.value;
	if (m_imagesInFlight.at(image_index)) {
		static_cast<void>(
		    m_core->getDevice().waitForFences(m_imagesInFlight.at(image_index), true, std::numeric_limits<uint64_t>::max())
		);
	}

	m_core->getDevice().resetFences(*frame.inFlight);
	m_imagesInFlight[image_index] = *frame.inFlight;

	recordFrame(frame, image_index);

	const vk::Semaphore wait_semaphore = *frame.imageAvailable;
	const std::array<vk::PipelineStageFlags, 1> wait_stages {vk::PipelineStageFlagBits::eColorAttachmentOutput};
	const vk::CommandBuffer command_buffer = *frame.commandBuffer;
	const vk::Semaphore signal_semaphore = *m_renderFinishedPerImage.at(image_index);
	const vk::SubmitInfo submit_info(1, &wait_semaphore, wait_stages.data(), 1, &command_buffer, 1, &signal_semaphore);
	m_core->getGraphicsQueue().submit(submit_info, *frame.inFlight);

	const auto present_result = m_outputTarget->present(image_index, signal_semaphore);

	// Advance to next frame
	m_currentFrame = (m_currentFrame + 1) % static_cast<uint32_t>(m_frames.size());

	if (present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR) {
		TOAST_WARN("VulkanRenderer", "Swapchain out of date or suboptimal on present; skipping frame");
		return;
	}
	if (present_result != vk::Result::eSuccess) {
		throw std::runtime_error("Toast Engine Error: Failed to present the current output image!");
	}
}

auto VulkanRenderer::resize(vk::Extent2D extent) -> void {
	m_outputTarget->recreate(extent);
	const auto image_count = m_outputTarget->getImageCount();
	m_imagesInFlight.assign(image_count, vk::Fence {});
	createPerImageSync();
	m_imageLayouts.assign(image_count, vk::ImageLayout::eUndefined);
}

}
