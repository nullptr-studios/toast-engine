/// @file VulkanRenderer.cpp
/// @author dario
/// @date 16/05/2026.

#include "VulkanRenderer.hpp"

#include "toast/log.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace toast::renderer {

namespace {

auto colorAttachmentRange() -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
}

// FIXME: Just Debugging
auto frameUniformSize() -> vk::DeviceSize {
	return sizeof(toast::renderer::VulkanRenderer::FrameUniformData);
}

auto depthAttachmentRange(vk::Format format) -> vk::ImageSubresourceRange {
	vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eDepth;
	switch (format) {
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint: aspect |= vk::ImageAspectFlagBits::eStencil; break;
		default: break;
	}
	return {aspect, 0, 1, 0, 1};
}

auto transitionImageLayout(
    vk::CommandBuffer command_buffer, vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
    vk::AccessFlags src_access_mask, vk::AccessFlags dst_access_mask, vk::PipelineStageFlags src_stage_mask,
    vk::PipelineStageFlags dst_stage_mask, vk::ImageSubresourceRange subresource_range
) -> void {
	const vk::ImageMemoryBarrier barrier(
	    src_access_mask,
	    dst_access_mask,
	    old_layout,
	    new_layout,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    image,
	    subresource_range
	);
	command_buffer.pipelineBarrier(src_stage_mask, dst_stage_mask, {}, {}, {}, barrier);
}

}

auto VulkanRenderer::selectDepthFormat(const VulkanCore& core) -> vk::Format {
	const std::array candidates {
	  vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint, vk::Format::eD16Unorm
	};

	for (const auto candidate : candidates) {
		const auto props = core.getPhysicalDevice().getFormatProperties(candidate);
		if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlags {}) {
			return candidate;
		}
	}

	TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: Failed to find a supported depth format!");
}

VulkanRenderer::VulkanRenderer(
    const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target, std::unique_ptr<VulkanPipeline> pipeline,
    uint32_t frames_in_flight
)
    : m_core(&core),
      m_outputTarget(std::move(output_target)),
      m_pipeline(std::move(pipeline)) {
	if (!m_outputTarget) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires an output target!");
	}

	if (!m_pipeline || !m_pipeline->isReady()) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires a valid pipeline!");
	}
	if (frames_in_flight == 0) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires at least one frame in flight!");
	}

	TOAST_TRACE("VulkanRenderer", "Creating renderer with {} frame(s) in flight", frames_in_flight);
	m_depthFormat = selectDepthFormat(core);

	createGraphicsCommandPool();
	createTransferCommandPool();
	// TODO: Compute Command Pool

	createFrameContexts(frames_in_flight);
	createPerImageSync();
	createDepthResources();
	createFrameUniformResources();
	m_imagesInFlight.assign(m_outputTarget->getImageCount(), vk::Fence {});
}

auto VulkanRenderer::createGraphicsCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getGraphicsQueueFamilyIndex()
	);
	m_commandPool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	TOAST_TRACE("VulkanRenderer", "Graphics command pool created (graphics family {})", m_core->getGraphicsQueueFamilyIndex());
}

auto VulkanRenderer::createTransferCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getTransferQueueFamilyIndex()
	);
	m_transferCommandPool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	TOAST_TRACE("VulkanRenderer", "Transfer command pool created (transfer family {})", m_core->getTransferQueueFamilyIndex());
}

auto VulkanRenderer::createFrameContexts(uint32_t frames_in_flight) -> void {
	m_frames.clear();
	m_frames.resize(frames_in_flight);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	const vk::FenceCreateInfo fence_ci(vk::FenceCreateFlagBits::eSignaled);
	const vk::CommandBufferAllocateInfo command_buffer_ci(*m_commandPool, vk::CommandBufferLevel::ePrimary, frames_in_flight);
	const vk::CommandBufferAllocateInfo transfer_command_buffer_ci(
	    *m_transferCommandPool, vk::CommandBufferLevel::ePrimary, frames_in_flight
	);
	auto allocated_command_buffers = m_core->getDevice().allocateCommandBuffers(command_buffer_ci);
	auto allocated_transfer_command_buffers = m_core->getDevice().allocateCommandBuffers(transfer_command_buffer_ci);

	for (uint32_t frame_index = 0; frame_index < frames_in_flight; ++frame_index) {
		m_frames[frame_index].commandBuffer = std::move(allocated_command_buffers[frame_index]);
		m_frames[frame_index].transferCommandBuffer = std::move(allocated_transfer_command_buffers[frame_index]);
		m_frames[frame_index].imageAvailable = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].transferFinished = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].inFlight = vk::raii::Fence(m_core->getDevice(), fence_ci);
	}

	TOAST_TRACE("VulkanRenderer", "Frame command buffers created: {}", frames_in_flight);
}

auto VulkanRenderer::createPerImageSync() -> void {
	const auto image_count = m_outputTarget->getImageCount();
	m_renderFinishedPerImage.clear();
	m_renderFinishedPerImage.reserve(image_count);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	for (uint32_t i = 0; i < image_count; ++i) {
		m_renderFinishedPerImage.emplace_back(m_core->getDevice(), semaphore_ci);
	}

	TOAST_TRACE("VulkanRenderer", "Per-image semaphores created: {}", image_count);
}

auto VulkanRenderer::createDepthResources() -> void {
	if (m_depthFormat == vk::Format::eUndefined) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires a valid depth format!");
	}

	const auto extent = m_outputTarget->getExtent();
	if (extent.width == 0 || extent.height == 0) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires a non-zero output extent for depth resources!");
	}

	m_depthResources.view.reset();
	m_depthResources.image.reset();

	// Depth image creation info
	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = m_depthFormat;
	image_ci.extent = vk::Extent3D {extent.width, extent.height, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 1;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	auto depth_image = m_core->getAllocator().createImage(image_ci, allocation_ci);
	m_depthResources.image.emplace(std::move(depth_image));

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_depthResources.image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = m_depthFormat;
	view_ci.subresourceRange = depthAttachmentRange(m_depthFormat);
	m_depthResources.view.emplace(m_core->getDevice(), view_ci);

	m_depthLayout = vk::ImageLayout::eUndefined;
	TOAST_TRACE(
	    "VulkanRenderer",
	    "Depth resources created at {}x{} with format {}",
	    extent.width,
	    extent.height,
	    vk::to_string(m_depthFormat)
	);
}

auto VulkanRenderer::createFrameUniformResources() -> void {
	if (!m_pipeline) {
		TOAST_CRITICAL(
		    "VulkanRenderer", "Toast Engine Error: VulkanRenderer requires a pipeline before creating frame uniform resources!"
		);
	}

	const vk::DeviceSize buffer_size = frameUniformSize();
	if (buffer_size == 0) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: Invalid frame uniform buffer size!");
	}

	vk::BufferCreateInfo staging_buffer_ci {};
	staging_buffer_ci.size = buffer_size;
	staging_buffer_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;
	staging_buffer_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo staging_allocation_ci {};
	staging_allocation_ci.usage = vma::MemoryUsage::eAutoPreferHost;

	// eHostAccessSequentialWrite for mapped memory
	staging_allocation_ci.flags =
	    vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	auto staging_buffer = m_core->getAllocator().createBuffer(staging_buffer_ci, staging_allocation_ci);
	m_frameUniformResources.stagingBuffer.emplace(std::move(staging_buffer));

	vk::BufferCreateInfo gpu_buffer_ci {};
	gpu_buffer_ci.size = buffer_size;
	gpu_buffer_ci.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst;
	gpu_buffer_ci.sharingMode = vk::SharingMode::eExclusive;

	vma::AllocationCreateInfo gpu_allocation_ci {};
	gpu_allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	auto gpu_buffer = m_core->getAllocator().createBuffer(gpu_buffer_ci, gpu_allocation_ci);
	m_frameUniformResources.gpuBuffer.emplace(std::move(gpu_buffer));

	const auto& device = m_core->getDevice();
	const auto descriptor_layout = *m_pipeline->getDescriptorSetLayout();
	const vk::DescriptorPoolSize pool_size(vk::DescriptorType::eUniformBuffer, 1);

	vk::DescriptorPoolCreateInfo pool_ci {};
	pool_ci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	pool_ci.maxSets = 1;
	pool_ci.poolSizeCount = 1;
	pool_ci.pPoolSizes = &pool_size;

	vk::raii::DescriptorPool new_pool(device, pool_ci);

	m_frameUniformResources.descriptorSet =
	    (*device).allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*new_pool, 1, &descriptor_layout))[0];

	m_descriptorPool = std::move(new_pool);

	vk::DescriptorBufferInfo buffer_info(*m_frameUniformResources.gpuBuffer, 0, buffer_size);
	vk::WriteDescriptorSet write_descriptor_set(
	    m_frameUniformResources.descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info
	);
	device.updateDescriptorSets(write_descriptor_set, nullptr);

	TOAST_INFO("VulkanRenderer", "Frame uniform resources created ({} bytes)", buffer_size);
}

// TODO: Pipeline should support push constants and uniform data, here uniforms is overkill
auto VulkanRenderer::updateFrameUniformData() -> void {
	if (!m_frameUniformResources.stagingBuffer.has_value() || !m_frameUniformResources.gpuBuffer.has_value()) {
		return;
	}

	m_frameUniformData.iResolution[0] = static_cast<float>(m_outputTarget->getExtent().width);
	m_frameUniformData.iResolution[1] = static_cast<float>(m_outputTarget->getExtent().height);
	m_frameUniformData.iTime = m_totalTime;
	m_frameUniformData.padding = 0.0f;

	auto& allocation = m_frameUniformResources.stagingBuffer->getAllocation();
	// With VMA_ALLOCATION_CREATE_MAPPED
	auto* mapped = allocation.getInfo().pMappedData;
	if (mapped) {
		std::memcpy(mapped, &m_frameUniformData, sizeof(m_frameUniformData));
		allocation.flush(0, sizeof(m_frameUniformData));
	}
}

// TODO: Make this dynamic, rn its hardcoded to a struct
auto VulkanRenderer::recordTransferPass(FrameContext& frame) -> void {
	if (!m_frameUniformResources.stagingBuffer.has_value() || !m_frameUniformResources.gpuBuffer.has_value()) {
		return;
	}

	frame.transferCommandBuffer.reset();
	const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	// Start transfer recording
	frame.transferCommandBuffer.begin(begin_info);

	// TODO: CHANGE THIS!!
	const vk::BufferCopy copy_region(0, 0, frameUniformSize());

	frame.transferCommandBuffer.copyBuffer(
	    **m_frameUniformResources.stagingBuffer, **m_frameUniformResources.gpuBuffer, copy_region
	);

	// End transfer recording
	frame.transferCommandBuffer.end();
}

auto VulkanRenderer::recordComputePass(FrameContext&, uint32_t) -> void {
	// TODO
}

auto VulkanRenderer::recordFrame(FrameContext& frame, uint32_t image_index) -> void {
	frame.commandBuffer.reset();
	const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	frame.commandBuffer.begin(begin_info);

	const vk::Image image = m_outputTarget->getColorImage(image_index);
	const vk::ImageLayout color_attachment_layout = vk::ImageLayout::eColorAttachmentOptimal;

	// transition to new image
	transitionImageLayout(
	    frame.commandBuffer,
	    image,
	    vk::ImageLayout::eUndefined,
	    color_attachment_layout,
	    vk::AccessFlags {},
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::PipelineStageFlagBits::eTopOfPipe,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    colorAttachmentRange()
	);

	// dynamic depth
	const vk::Image depth_image = m_depthResources.image ? **m_depthResources.image : VK_NULL_HANDLE;
	if (depth_image != VK_NULL_HANDLE && m_depthLayout != vk::ImageLayout::eDepthAttachmentOptimal) {
		transitionImageLayout(
		    frame.commandBuffer,
		    depth_image,
		    m_depthLayout,
		    vk::ImageLayout::eDepthAttachmentOptimal,
		    vk::AccessFlags {},
		    vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		    vk::PipelineStageFlagBits::eTopOfPipe,
		    vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
		    depthAttachmentRange(m_depthFormat)
		);
		m_depthLayout = vk::ImageLayout::eDepthAttachmentOptimal;
	}

	// clearing
	const vk::ClearValue clear_color(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 1.0f}));
	const vk::ClearValue clear_depth(vk::ClearDepthStencilValue {1.0f, 0});
	vk::RenderingAttachmentInfo color_attachment_info {};
	color_attachment_info.imageView = *m_outputTarget->getColorAttachment(image_index);
	color_attachment_info.imageLayout = color_attachment_layout;
	color_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	color_attachment_info.resolveImageView = nullptr;
	color_attachment_info.resolveImageLayout = vk::ImageLayout::eUndefined;
	color_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	color_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	color_attachment_info.clearValue = clear_color;

	// clearing depth
	vk::RenderingAttachmentInfo depth_attachment_info {};
	if (m_depthResources.view.has_value()) {
		depth_attachment_info.imageView = **m_depthResources.view;
		depth_attachment_info.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		depth_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
		depth_attachment_info.storeOp = vk::AttachmentStoreOp::eDontCare;
		depth_attachment_info.clearValue = clear_depth;
	}

	// setup to initialize rendering
	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, m_outputTarget->getExtent());
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment_info;
	if (m_depthResources.view.has_value()) {
		rendering_info.pDepthAttachment = &depth_attachment_info;
	}
	frame.commandBuffer.beginRendering(rendering_info);

	// TODO: RECORDING LOOP

	// Bind pipeline
	// TODO: EXPAND ON THIS
	frame.commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline->getPipeline());

	// Set dynamic viewport and scissor to match the current output extent
	const auto extent = m_outputTarget->getExtent();
	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
	const vk::Rect2D scissor({0, 0}, extent);
	frame.commandBuffer.setViewport(0, std::array {viewport});
	frame.commandBuffer.setScissor(0, std::array {scissor});
	if (m_frameUniformResources.descriptorSet != VK_NULL_HANDLE) {
		const std::array<vk::DescriptorSet, 1> descriptor_sets {m_frameUniformResources.descriptorSet};
		frame.commandBuffer.bindDescriptorSets(
		    vk::PipelineBindPoint::eGraphics, m_pipeline->getPipelineLayout(), 0, descriptor_sets, {}
		);
	}

	// TODO: Sending vertices!!!
	// Draw fullscreen
	frame.commandBuffer.draw(6, 1, 0, 0);

	// End rendering
	frame.commandBuffer.endRendering();

	// Transition the image to PresentSrcKHR
	const vk::ImageLayout present_layout = vk::ImageLayout::ePresentSrcKHR;
	transitionImageLayout(
	    frame.commandBuffer,
	    image,
	    color_attachment_layout,
	    present_layout,
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::AccessFlags {},
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    vk::PipelineStageFlagBits::eBottomOfPipe,
	    colorAttachmentRange()
	);

	// End frame record
	frame.commandBuffer.end();
}

auto VulkanRenderer::drawFrame() -> void {
	if (m_frames.empty()) {
		return;
	}

	// FIXME: DEBUGGING PURPOSES
	m_totalTime += 0.0016f;

	// Frame rendering
	auto& frame = m_frames[m_currentFrame];
	m_core->getDevice().waitForFences(*frame.inFlight, true, std::numeric_limits<uint64_t>::max());

	const auto acquired =
	    m_outputTarget->acquireNextImage(std::numeric_limits<uint64_t>::max(), *frame.imageAvailable, VK_NULL_HANDLE);

	if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
		TOAST_WARN("VulkanRenderer", "Swapchain out of date on acquire; skipping frame");
		return;
	}
	if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: Failed to acquire the next output image!");
	}

	const uint32_t image_index = acquired.value;
	if (m_imagesInFlight.at(image_index)) {
		m_core->getDevice().waitForFences(m_imagesInFlight.at(image_index), true, std::numeric_limits<uint64_t>::max());
	}

	m_core->getDevice().resetFences(*frame.inFlight);
	m_imagesInFlight[image_index] = *frame.inFlight;

	// Recording the shit
	updateFrameUniformData();
	recordTransferPass(frame);
	recordComputePass(frame, image_index);
	recordFrame(frame, image_index);

	// Starting transfer submission
	const vk::Semaphore transfer_wait_semaphore = *frame.transferFinished;
	const vk::CommandBuffer transfer_command_buffer = *frame.transferCommandBuffer;
	const vk::SubmitInfo transfer_submit_info(0, nullptr, nullptr, 1, &transfer_command_buffer, 1, &transfer_wait_semaphore);
	m_core->getTransferQueue().submit(transfer_submit_info);

	// Starting graghics submission
	// Needs to wait for the transfer to complete and if an image is available
	const std::array wait_semaphores {*frame.imageAvailable, transfer_wait_semaphore};
	const std::array<vk::PipelineStageFlags, 2> wait_stages {
	  vk::PipelineStageFlagBits::eColorAttachmentOutput,
	  vk::PipelineStageFlagBits::eFragmentShader    // FIXME: some errors might arrise because of this
	};
	const vk::CommandBuffer command_buffer = *frame.commandBuffer;
	const vk::Semaphore signal_semaphore = *m_renderFinishedPerImage.at(image_index);
	const vk::SubmitInfo submit_info(
	    wait_semaphores.size(), wait_semaphores.data(), wait_stages.data(), 1, &command_buffer, 1, &signal_semaphore
	);
	m_core->getGraphicsQueue().submit(submit_info, *frame.inFlight);

	// Present onto target texture
	const auto present_result = m_outputTarget->present(image_index, signal_semaphore);

	// Advance to next frame
	m_currentFrame = (m_currentFrame + 1) % static_cast<uint32_t>(m_frames.size());

	if (present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR) {
		TOAST_WARN("VulkanRenderer", "Swapchain out of date or suboptimal on present; skipping frame");
		return;
	}
	if (present_result != vk::Result::eSuccess) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: Failed to present the current output image!");
	}
}

auto VulkanRenderer::resize(vk::Extent2D extent) -> void {
	if (extent.width == 0 || extent.height == 0) {
		TOAST_WARN("VulkanRenderer", "Ignoring resize to invalid extent {}x{}", extent.width, extent.height);
		return;
	}

	// wait for free device
	m_core->getDevice().waitIdle();
	try {
		m_outputTarget->recreate(extent);
		const auto image_count = m_outputTarget->getImageCount();
		m_imagesInFlight.assign(image_count, vk::Fence {});
		createPerImageSync();
		createDepthResources();
		m_currentFrame = 0;
	} catch (const std::exception& e) {
		TOAST_CRITICAL("VulkanRenderer", "Failed to recreate output target on resize: {}", e.what());
	}
}

}
