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

VulkanRenderer* VulkanRenderer::instance = nullptr;

namespace {

auto colorAttachmentRange() -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
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

VulkanRenderer::VulkanRenderer(const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target)
    : m_core(&core),
      m_outputTarget(std::move(output_target)) {
	instance = this;
	if (!m_outputTarget) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires an output target!");
	}

	if (kFramesInFlight == 0) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires at least one frame in flight!");
	}

	TOAST_TRACE("VulkanRenderer", "Creating renderer with {} frame(s) in flight", kFramesInFlight);
	m_depthFormat = selectDepthFormat(core);

	createGraphicsCommandPool();
	createTransferCommandPool();
	// TODO: Compute Command Pool

	createFrameContexts();
	createPerImageSync();
	createDepthResources();
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

auto VulkanRenderer::createFrameContexts() -> void {
	m_frames.clear();
	m_frames.resize(kFramesInFlight);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	const vk::FenceCreateInfo fence_ci(vk::FenceCreateFlagBits::eSignaled);
	const vk::CommandBufferAllocateInfo command_buffer_ci(*m_commandPool, vk::CommandBufferLevel::ePrimary, kFramesInFlight);
	const vk::CommandBufferAllocateInfo transfer_command_buffer_ci(
	    *m_transferCommandPool, vk::CommandBufferLevel::ePrimary, kFramesInFlight
	);
	auto allocated_command_buffers = m_core->getDevice().allocateCommandBuffers(command_buffer_ci);
	auto allocated_transfer_command_buffers = m_core->getDevice().allocateCommandBuffers(transfer_command_buffer_ci);

	for (uint32_t frame_index = 0; frame_index < kFramesInFlight; ++frame_index) {
		m_frames[frame_index].commandBuffer = std::move(allocated_command_buffers[frame_index]);
		m_frames[frame_index].transferCommandBuffer = std::move(allocated_transfer_command_buffers[frame_index]);
		m_frames[frame_index].imageAvailable = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].transferFinished = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].inFlight = vk::raii::Fence(m_core->getDevice(), fence_ci);
	}

	TOAST_TRACE("VulkanRenderer", "Frame command buffers created: {}", kFramesInFlight);
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

auto VulkanRenderer::recordTransferPass(FrameContext& frame) -> void {
	// if (!m_frameUniformResources.stagingBuffer.has_value() || !m_frameUniformResources.gpuBuffer.has_value()) {
	// 	return;
	// }
	//
	// frame.transferCommandBuffer.reset();
	// const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	// // Start transfer recording
	// frame.transferCommandBuffer.begin(begin_info);
	//
	// // TODO: CHANGE THIS!!
	// const vk::BufferCopy copy_region(0, 0, frameUniformSize());
	//
	// frame.transferCommandBuffer.copyBuffer(
	//     **m_frameUniformResources.stagingBuffer, **m_frameUniformResources.gpuBuffer, copy_region
	// );
	//
	// // End transfer recording
	// frame.transferCommandBuffer.end();
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

	// Set dynamic viewport and scissor to match the current output extent
	const auto extent = m_outputTarget->getExtent();
	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
	const vk::Rect2D scissor({0, 0}, extent);
	frame.commandBuffer.setViewport(0, std::array {viewport});
	frame.commandBuffer.setScissor(0, std::array {scissor});
	// if (m_frameUniformResources.descriptorSet != VK_NULL_HANDLE) {
	// 	const std::array<vk::DescriptorSet, 1> descriptor_sets {m_frameUniformResources.descriptorSet};
	// 	frame.commandBuffer.bindDescriptorSets(
	// 	    vk::PipelineBindPoint::eGraphics, m_pipeline->getPipelineLayout(), 0, descriptor_sets, {}
	// 	);
	// }

	// record loop
	for (auto& pass : m_renderPasses) {
		pass->record(*frame.commandBuffer, m_currentFrame, image_index);
	}

	// // TODO: Sending vertices!!!
	// // Draw fullscreen
	// frame.commandBuffer.draw(6, 1, 0, 0);

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

void VulkanRenderer::processPendingUploads() {
	auto& device = m_core->getDevice();

	for (auto it = m_pendingUploads.begin(); it != m_pendingUploads.end();) {
		const auto status = vkGetFenceStatus(*device, *it->completionFence);

		if (status == VkResult::VK_SUCCESS) {
			// Fence signaled, staging buffers can die here
			it = m_pendingUploads.erase(it);
		} else if (status == VK_NOT_READY) {
			++it;
		} else {
			TOAST_CRITICAL("VulkanRenderer", "Fence status query failed during mesh upload cleanup");
		}
	}
}

auto VulkanRenderer::drawFrame() -> void {
	if (m_frames.empty()) {
		return;
	}

	processPendingUploads();

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

	// Update the Render passes TODO: Move outside of renderloop
	for (auto& pass : m_renderPasses) {
		pass->update(m_currentFrame, 0.016f);
	}

	// Recording the shit
	// recordTransferPass(frame); TODO with textures and meshes
	// recordComputePass(frame, image_index);
	recordFrame(frame, image_index);

	// Starting transfer submission
	// const vk::Semaphore transfer_wait_semaphore = *frame.transferFinished;
	// const vk::CommandBuffer transfer_command_buffer = *frame.transferCommandBuffer;
	// const vk::SubmitInfo transfer_submit_info(0, nullptr, nullptr, 1, &transfer_command_buffer, 1, &transfer_wait_semaphore);
	// m_core->getTransferQueue().submit(transfer_submit_info);

	// Starting graghics submission
	// Needs to wait for the transfer to complete and if an image is available
	const std::array wait_semaphores {*frame.imageAvailable};
	const std::array<vk::PipelineStageFlags, 1> wait_stages {
	  vk::PipelineStageFlagBits::eColorAttachmentOutput,
	  /*vk::PipelineStageFlagBits::eAllCommands*/    // FIXME: This will be problematic in the future
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

void VulkanRenderer::addRenderPass(std::unique_ptr<IRenderPass> pass) {
	m_renderPasses.push_back(std::move(pass));
}

void VulkanRenderer::queueMeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data) {
	// Create the final GPU buffers in the mesh
	mesh.create(*m_core, data, m_core->getGraphicsQueueFamilyIndex(), m_core->getTransferQueueFamilyIndex());

	PendingMeshUpload job;
	job.mesh = &mesh;

	const vk::DeviceSize vertexSize = data.vertices.size_bytes();
	const vk::DeviceSize indexSize = data.indices.size_bytes();

	// Create vertex staging buffer
	{
		vk::BufferCreateInfo stagingCI {};
		stagingCI.size = vertexSize;
		stagingCI.usage = vk::BufferUsageFlagBits::eTransferSrc;
		stagingCI.sharingMode = vk::SharingMode::eExclusive;

		vma::AllocationCreateInfo allocCI {};
		allocCI.usage = vma::MemoryUsage::eAutoPreferHost;
		allocCI.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		job.vertexStaging = m_core->getAllocator().createBuffer(stagingCI, allocCI);

		auto& allocation = job.vertexStaging.getAllocation();
		void* mapped = allocation.getInfo().pMappedData;
		if (!mapped) {
			TOAST_CRITICAL("VulkanRenderer", "Vertex staging buffer is not mapped");
		}

		std::memcpy(mapped, data.vertices.data(), vertexSize);
		allocation.flush(0, vertexSize);
	}

	// Create index staging buffer
	{
		vk::BufferCreateInfo stagingCI {};
		stagingCI.size = indexSize;
		stagingCI.usage = vk::BufferUsageFlagBits::eTransferSrc;
		stagingCI.sharingMode = vk::SharingMode::eExclusive;

		vma::AllocationCreateInfo allocCI {};
		allocCI.usage = vma::MemoryUsage::eAutoPreferHost;
		allocCI.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		job.indexStaging = m_core->getAllocator().createBuffer(stagingCI, allocCI);

		auto& allocation = job.indexStaging.getAllocation();
		void* mapped = allocation.getInfo().pMappedData;
		if (!mapped) {
			TOAST_CRITICAL("VulkanRenderer", "Index staging buffer is not mapped");
		}

		std::memcpy(mapped, data.indices.data(), indexSize);
		allocation.flush(0, indexSize);
	}

	// Record the transfer command buffer
	auto& transferCmd = m_frames[m_currentFrame].transferCommandBuffer;
	transferCmd.reset();

	const vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	transferCmd.begin(beginInfo);

	mesh.recordUpload(*transferCmd, *job.vertexStaging, *job.indexStaging);

	transferCmd.end();

	// Submit to transfer queue with a fence so staging buffers stay alive
	job.completionFence = vk::raii::Fence(m_core->getDevice(), vk::FenceCreateInfo {});

	const vk::CommandBuffer rawTransferCmd = *transferCmd;
	const vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &rawTransferCmd);

	m_core->getTransferQueue().submit(submitInfo, *job.completionFence);

	// Keep staging alive until the fence signals
	m_pendingUploads.push_back(std::move(job));
}
}
