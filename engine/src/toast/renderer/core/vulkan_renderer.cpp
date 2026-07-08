/// @file VulkanRenderer.cpp
/// @author dario
/// @date 16/05/2026.

#include "vulkan_renderer.hpp"

#include "toast/log.hpp"
#include "toast/time.hpp"
#include "toast/window/window_events.hpp"

#include <array>
#include <cstring>
#include <iostream>
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

auto packExtent(vk::Extent2D extent) -> uint64_t {
	return (static_cast<uint64_t>(extent.width) << 32u) | static_cast<uint64_t>(extent.height);
}

auto unpackExtent(uint64_t packed) -> vk::Extent2D {
	return {static_cast<uint32_t>(packed >> 32u), static_cast<uint32_t>(packed & 0xFFFFFFFFu)};
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
      m_output_target(std::move(output_target)) {
	instance = this;
	if (!m_output_target) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires an output target!");
	}

	if (kFramesInFlight == 0) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires at least one frame in flight!");
	}

	TOAST_TRACE("VulkanRenderer", "Creating renderer with {} frame(s) in flight", kFramesInFlight);
	m_depth_format = selectDepthFormat(core);

	createGraphicsCommandPool();
	createTransferCommandPool();
	// TODO: Compute Command Pool

	createFrameContexts();
	createPerImageSync();
	createDepthResources();
	const auto output_image_count = m_output_target->getImageCount();
	m_images_in_flight.assign(output_image_count, vk::Fence {});
	m_output_image_layouts.assign(output_image_count, vk::ImageLayout::eUndefined);

	createDescriptorPool();

	// Create FrameData UBO
	createFrameResources();
}

VulkanRenderer::~VulkanRenderer() {
	stop();
	instance = nullptr;
}

auto VulkanRenderer::createGraphicsCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getGraphicsQueueFamilyIndex()
	);
	m_command_pool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	TOAST_TRACE("VulkanRenderer", "Graphics command pool created (graphics family {})", m_core->getGraphicsQueueFamilyIndex());
}

auto VulkanRenderer::createTransferCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getTransferQueueFamilyIndex()
	);
	m_transfer_command_pool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	TOAST_TRACE("VulkanRenderer", "Transfer command pool created (transfer family {})", m_core->getTransferQueueFamilyIndex());
}

auto VulkanRenderer::createFrameContexts() -> void {
	m_frames.clear();
	m_frames.resize(kFramesInFlight);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	const vk::FenceCreateInfo fence_ci(vk::FenceCreateFlagBits::eSignaled);
	const vk::CommandBufferAllocateInfo command_buffer_ci(*m_command_pool, vk::CommandBufferLevel::ePrimary, kFramesInFlight);
	const vk::CommandBufferAllocateInfo transfer_command_buffer_ci(
	    *m_transfer_command_pool, vk::CommandBufferLevel::ePrimary, kFramesInFlight
	);
	auto allocated_command_buffers = m_core->getDevice().allocateCommandBuffers(command_buffer_ci);
	auto allocated_transfer_command_buffers = m_core->getDevice().allocateCommandBuffers(transfer_command_buffer_ci);

	for (uint32_t frame_index = 0; frame_index < kFramesInFlight; ++frame_index) {
		m_frames[frame_index].command_buffer = std::move(allocated_command_buffers[frame_index]);
		m_frames[frame_index].transfer_command_buffer = std::move(allocated_transfer_command_buffers[frame_index]);
		m_frames[frame_index].image_available = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].transfer_finished = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].in_flight = vk::raii::Fence(m_core->getDevice(), fence_ci);
	}

	TOAST_TRACE("VulkanRenderer", "Frame command buffers created: {}", kFramesInFlight);
}

auto VulkanRenderer::createPerImageSync() -> void {
	const auto image_count = m_output_target->getImageCount();
	m_render_finished_per_image.clear();
	m_render_finished_per_image.reserve(image_count);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	for (uint32_t i = 0; i < image_count; ++i) {
		m_render_finished_per_image.emplace_back(m_core->getDevice(), semaphore_ci);
	}

	TOAST_TRACE("VulkanRenderer", "Per-image semaphores created: {}", image_count);
}

auto VulkanRenderer::createDepthResources() -> void {
	if (m_depth_format == vk::Format::eUndefined) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires a valid depth format!");
	}

	const auto extent = m_output_target->getExtent();
	if (extent.width == 0 || extent.height == 0) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires a non-zero output extent for depth resources!");
	}

	m_depth_resources.view.reset();
	m_depth_resources.image.reset();

	// Depth image creation info
	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = m_depth_format;
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
	m_depth_resources.image.emplace(std::move(depth_image));

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_depth_resources.image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = m_depth_format;
	view_ci.subresourceRange = depthAttachmentRange(m_depth_format);
	m_depth_resources.view.emplace(m_core->getDevice(), view_ci);

	m_depth_layout = vk::ImageLayout::eUndefined;
	TOAST_TRACE(
	    "VulkanRenderer",
	    "Depth resources created at {}x{} with format {}",
	    extent.width,
	    extent.height,
	    vk::to_string(m_depth_format)
	);
}

void VulkanRenderer::createDescriptorPool() {
	std::array poolSizes {
	  vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1024),

	  vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4096),

	  vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1024)
	};
	vk::DescriptorPoolCreateInfo poolCI {};
	poolCI.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

	poolCI.maxSets = 8192;

	poolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());

	poolCI.pPoolSizes = poolSizes.data();

	m_descriptor_pool = vk::raii::DescriptorPool(m_core->getDevice(), poolCI);
}

auto VulkanRenderer::recordTransferPass(FrameContext& frame) -> void {
	// if (!m_frameUniformResources.stagingBuffer.has_value() || !m_frameUniformResources.gpuBuffer.has_value()) {
	// 	return;
	// }
	//
	// frame.transfer_command_buffer.reset();
	// const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	// // Start transfer recording
	// frame.transfer_command_buffer.begin(begin_info);
	//
	// // TODO: CHANGE THIS!!
	// const vk::BufferCopy copy_region(0, 0, frameUniformSize());
	//
	// frame.transfer_command_buffer.copyBuffer(
	//     **m_frameUniformResources.stagingBuffer, **m_frameUniformResources.gpuBuffer, copy_region
	// );
	//
	// // End transfer recording
	// frame.transfer_command_buffer.end();
}

auto VulkanRenderer::recordComputePass(FrameContext&, uint32_t) -> void {
	// TODO
}

auto VulkanRenderer::recordFrame(FrameContext& frame, uint32_t image_index) -> void {
	frame.command_buffer.reset();
	const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	frame.command_buffer.begin(begin_info);

	const vk::Image image = m_output_target->getColorImage(image_index);
	const vk::ImageLayout color_attachment_layout = vk::ImageLayout::eColorAttachmentOptimal;
	const vk::ImageLayout previous_layout = m_output_image_layouts.at(image_index);

	vk::AccessFlags src_access_mask {};
	vk::PipelineStageFlags src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;

	switch (previous_layout) {
		case vk::ImageLayout::eUndefined:
			src_access_mask = vk::AccessFlags {};
			src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
			break;
		case vk::ImageLayout::ePresentSrcKHR:
			src_access_mask = vk::AccessFlags {};
			src_stage_mask = vk::PipelineStageFlagBits::eBottomOfPipe;
			break;
		case vk::ImageLayout::eTransferSrcOptimal:
			src_access_mask = vk::AccessFlagBits::eTransferRead;
			src_stage_mask = vk::PipelineStageFlagBits::eTransfer;
			break;
		case vk::ImageLayout::eColorAttachmentOptimal:
			src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
			src_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			break;
		default:
			src_access_mask = vk::AccessFlags {};
			src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
			break;
	}

	transitionImageLayout(
	    frame.command_buffer,
	    image,
	    previous_layout,
	    color_attachment_layout,
	    src_access_mask,
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    src_stage_mask,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    colorAttachmentRange()
	);

	// dynamic depth
	const vk::Image depth_image = m_depth_resources.image ? **m_depth_resources.image : VK_NULL_HANDLE;
	if (depth_image != VK_NULL_HANDLE && m_depth_layout != vk::ImageLayout::eDepthAttachmentOptimal) {
		transitionImageLayout(
		    frame.command_buffer,
		    depth_image,
		    m_depth_layout,
		    vk::ImageLayout::eDepthAttachmentOptimal,
		    vk::AccessFlags {},
		    vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		    vk::PipelineStageFlagBits::eTopOfPipe,
		    vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
		    depthAttachmentRange(m_depth_format)
		);
		m_depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;
	}

	// clearing
	const vk::ClearValue clear_color(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 1.0f}));
	const vk::ClearValue clear_depth(vk::ClearDepthStencilValue {1.0f, 0});
	vk::RenderingAttachmentInfo color_attachment_info {};
	color_attachment_info.imageView = *m_output_target->getColorAttachment(image_index);
	color_attachment_info.imageLayout = color_attachment_layout;
	color_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	color_attachment_info.resolveImageView = nullptr;
	color_attachment_info.resolveImageLayout = vk::ImageLayout::eUndefined;
	color_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	color_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	color_attachment_info.clearValue = clear_color;

	// clearing depth
	vk::RenderingAttachmentInfo depth_attachment_info {};
	if (m_depth_resources.view.has_value()) {
		depth_attachment_info.imageView = **m_depth_resources.view;
		depth_attachment_info.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		depth_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
		depth_attachment_info.storeOp = vk::AttachmentStoreOp::eDontCare;
		depth_attachment_info.clearValue = clear_depth;
	}

	// setup to initialize rendering
	vk::RenderingInfo rendering_info {};
	rendering_info.renderArea = vk::Rect2D({0, 0}, m_output_target->getExtent());
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment_info;
	if (m_depth_resources.view.has_value()) {
		rendering_info.pDepthAttachment = &depth_attachment_info;
	}
	frame.command_buffer.beginRendering(rendering_info);

	// Set dynamic viewport and scissor to match the current output extent
	const auto extent = m_output_target->getExtent();
	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
	const vk::Rect2D scissor({0, 0}, extent);
	frame.command_buffer.setViewport(0, std::array {viewport});
	frame.command_buffer.setScissor(0, std::array {scissor});
	// if (m_frameUniformResources.descriptorSet != VK_NULL_HANDLE) {
	// 	const std::array<vk::DescriptorSet, 1> descriptor_sets {m_frameUniformResources.descriptorSet};
	// 	frame.command_buffer.bindDescriptorSets(
	// 	    vk::PipelineBindPoint::eGraphics, m_pipeline->getPipelineLayout(), 0, descriptor_sets, {}
	// 	);
	// }

	// record loop
	for (auto& pass : m_render_passes) {
		pass->record(*frame.command_buffer, m_current_frame, image_index);
	}

	// End rendering
	frame.command_buffer.endRendering();

	m_output_target->recordFinalize(frame.command_buffer, image_index);
	m_output_image_layouts.at(image_index) =
	    m_output_target->usesAcquirePresentSemaphores() ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eTransferSrcOptimal;

	// End frame record
	frame.command_buffer.end();
}

void VulkanRenderer::createFrameResources() {
	m_frame_ubo_res.resize(kFramesInFlight);
	m_frame_ubos.resize(kFramesInFlight);

	const auto& device = m_core->getDevice();

	const vk::DeviceSize bufferSize = sizeof(FrameUBO);

	// Create per-frame UBO buffers only. Descriptor sets are allocated by individual render passes (MeshPass).
	for (auto& frame : m_frame_ubo_res) {
		// create ubo
		vk::BufferCreateInfo bufferCI {};
		bufferCI.size = bufferSize;
		bufferCI.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		// allocation
		vma::AllocationCreateInfo allocCI {};
		allocCI.usage = vma::MemoryUsage::eAutoPreferHost;

		allocCI.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		// create buffer
		frame.gpuBuffer.emplace(m_core->getAllocator().createBuffer(bufferCI, allocCI));

		// no staging buffer

		// Leave descriptorSet empty; render passes will allocate and manage their own descriptor sets
	}
}

void VulkanRenderer::updateFrameResources(uint32_t frameIndex, RenderFrame& frameData) {
	m_frame_ubos[frameIndex] = frameData.frame_data;

	auto& allocation = m_frame_ubo_res[frameIndex].gpuBuffer->getAllocation();
	// With VMA_ALLOCATION_CREATE_MAPPED
	auto* mapped = allocation.getInfo().pMappedData;

	if (mapped) {
		std::memcpy(mapped, &m_frame_ubos[frameIndex], sizeof(FrameUBO));

		allocation.flush(0, sizeof(FrameUBO));
	}
}

auto VulkanRenderer::drawFrame(RenderFrame& frameData) -> void {
	ZoneScoped;
	if (m_frames.empty()) {
		return;
	}

	// process upload fences
	processPendingUploads();

	// upload next batch of resources
	flushResourceUploads();

	// Frame rendering
	auto& frame = m_frames[m_current_frame];
	m_core->getDevice().waitForFences(*frame.in_flight, true, std::numeric_limits<uint64_t>::max());
	if (frame.has_submitted) {
		m_output_target->onImageRenderComplete(frame.last_image_index);
		frame.has_submitted = false;
	}

	const auto acquired =
	    m_output_target->acquireNextImage(std::numeric_limits<uint64_t>::max(), *frame.image_available, VK_NULL_HANDLE);

	if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
		TOAST_WARN("VulkanRenderer", "Swapchain out of date on acquire; recreating");
		applyResize(m_output_target->getExtent());
		return;
	}
	if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: Failed to acquire the next output image!");
	}

	const uint32_t image_index = acquired.value;
	if (m_images_in_flight.at(image_index)) {
		m_core->getDevice().waitForFences(m_images_in_flight.at(image_index), true, std::numeric_limits<uint64_t>::max());
	}

	m_core->getDevice().resetFences(*frame.in_flight);
	m_images_in_flight[image_index] = *frame.in_flight;

	// Update FrameData
	updateFrameResources(m_current_frame, frameData);    // FIXME: dt

	m_rendering_frame = &frameData;

	// Update the Render passes TODO: Move outside of renderloop
	for (auto& pass : m_render_passes) {
		pass->update(m_current_frame, Time::get().renderDelta());
	}

	// Recording the shit
	// recordTransferPass(frame); TODO with textures and meshes
	// recordComputePass(frame, image_index);
	recordFrame(frame, image_index);

	// Starting transfer submission
	// const vk::Semaphore transfer_wait_semaphore = *frame.transfer_finished;
	// const vk::CommandBuffer transfer_command_buffer = *frame.transfer_command_buffer;
	// const vk::SubmitInfo transfer_submit_info(0, nullptr, nullptr, 1, &transfer_command_buffer, 1, &transfer_wait_semaphore);
	// m_core->getTransferQueue().submit(transfer_submit_info);

	// Starting graghics submission
	// Always wait for the transfer
	const bool present_sync = m_output_target->usesAcquirePresentSemaphores();

	const vk::CommandBuffer command_buffer = *frame.command_buffer;
	const vk::Semaphore signal_semaphore = *m_render_finished_per_image.at(image_index);
	const vk::Semaphore wait_semaphore = *frame.image_available;
	const vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	vk::SubmitInfo submit_info {};
	if (present_sync) {
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &wait_semaphore;
		submit_info.pWaitDstStageMask = &wait_stage;
	}
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &signal_semaphore;
	m_core->getGraphicsQueue().submit(submit_info, *frame.in_flight);

	// Present onto target texture
	const auto present_result = m_output_target->present(image_index, signal_semaphore);
	frame.last_image_index = image_index;
	frame.has_submitted = true;

	// Advance to next frame
	m_current_frame = (m_current_frame + 1) % static_cast<uint32_t>(m_frames.size());

	if (present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR) {
		TOAST_WARN("VulkanRenderer", "Swapchain out of date or suboptimal on present; recreating");
		m_rendering_frame = nullptr;
		applyResize(m_output_target->getExtent());
		return;
	}
	if (present_result != vk::Result::eSuccess) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: Failed to present the current output image!");
	}

	m_rendering_frame = nullptr;
}

void VulkanRenderer::mainRenderThread() {
	tracy::SetThreadName("Renderer Thread");
	while (m_running.load(std::memory_order_acquire)) {
		ZoneScopedN("VulkanRenderer::mainRenderThread");

		const uint64_t pending_resize = m_pending_resize_packed.exchange(kNoPendingResize, std::memory_order_acq_rel);
		if (pending_resize != kNoPendingResize) {
			const auto extent = unpackExtent(pending_resize);
			if (extent.width > 0 && extent.height > 0) {
				applyResizeInternal(extent);
			}
		}

		RenderFrame frameToDraw;
		bool consumedQueuedFrame = false;

		{
			std::unique_lock lock(m_queue_mutex);

			if (m_ready_frames.empty()) {
				if (!m_has_cached_frame) {
					m_frame_cv.wait(lock, [this] {
						return !m_ready_frames.empty() || !m_running ||
						       m_pending_resize_packed.load(std::memory_order_acquire) != kNoPendingResize;
					});
					if (!m_running) {
						return;
					}
				}
			}

			if (!m_ready_frames.empty()) {
				const auto frameIndex = m_ready_frames.front();
				m_ready_frames.pop();
				frameToDraw = m_render_frames[frameIndex];
				m_cached_frame = frameToDraw;
				m_has_cached_frame = true;
				consumedQueuedFrame = true;
			} else if (m_has_cached_frame) {
				frameToDraw = m_cached_frame;
			} else {
				continue;
			}
		}

		Time::get().renderTick();

		// m_core->getRenderDocAPI()->StartFrameCapture(nullptr, nullptr);

		drawFrame(frameToDraw);

		// m_core->getRenderDocAPI()->EndFrameCapture(nullptr, nullptr);

		if (consumedQueuedFrame) {
			m_free_frames.release();
		}
	}
}

void VulkanRenderer::start() {
	TOAST_TRACE("VulkanRenderer", "Starting renderer");

	m_running.store(true, std::memory_order_release);

	m_render_thread = std::thread([this] { mainRenderThread(); });
}

void VulkanRenderer::submitFrame() {
	// TracyMessage("Swapped frame resource", 256);
	{
		std::lock_guard lock(m_queue_mutex);

		m_ready_frames.push(m_write_index);

		m_write_index = (m_write_index + 1) % kRenderFrames;
	}

	m_frame_cv.notify_one();
}

void VulkanRenderer::stop() {
	const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
	if (!was_running) {
		return;
	}

	// First ensure the GPU is idle so fences can reliably complete; then wake the thread.
	if (m_core) {
		m_core->getDevice().waitIdle();
	}

	m_frame_cv.notify_all();

	if (m_render_thread.joinable()) {
		m_render_thread.join();
	}
}

auto VulkanRenderer::applyResize(vk::Extent2D extent) -> void {
	if (extent.width == 0 || extent.height == 0) {
		return;
	}

	m_pending_resize_packed.store(packExtent(extent), std::memory_order_release);
	m_frame_cv.notify_one();
}

auto VulkanRenderer::applyResizeInternal(vk::Extent2D extent) -> void {
	m_core->getDevice().waitIdle();
	for (auto& frame : m_frames) {
		if (frame.has_submitted) {
			m_output_target->onImageRenderComplete(frame.last_image_index);
			frame.has_submitted = false;
		}
	}
	try {
		m_output_target->recreate(extent);
		const auto image_count = m_output_target->getImageCount();
		m_images_in_flight.assign(image_count, vk::Fence {});
		m_output_image_layouts.assign(image_count, vk::ImageLayout::eUndefined);
		createPerImageSync();
		createDepthResources();
		m_current_frame = 0;
	} catch (const std::exception& e) {
		TOAST_CRITICAL("VulkanRenderer", "Failed to recreate output target on resize: {}", e.what());
	}
}

void VulkanRenderer::addRenderPass(std::unique_ptr<IRenderPass> pass) {
	m_render_passes.push_back(std::move(pass));
}

void VulkanRenderer::queueResourceUpload(std::unique_ptr<PendingResourceUpload> uploadJob) {
	uploadJob->build(*m_core);

	std::lock_guard<std::mutex> lock(m_upload_mutex);
	m_upload_staging.push_back(std::move(uploadJob));
}

void VulkanRenderer::processPendingUploads() {
	auto& device = m_core->getDevice();

	while (!m_pending_uploads.empty()) {
		auto& oldestBatch = m_pending_uploads.front();

		const auto status = vkGetFenceStatus(*device, *oldestBatch.completionFence);

		if (status == VkResult::VK_SUCCESS) {
			for (auto& job : oldestBatch.jobs) {
				job->finished();
			}

			m_pending_uploads.pop();
		} else {
			break;
		}
	}
}

void VulkanRenderer::flushResourceUploads() {
	std::vector<std::unique_ptr<PendingResourceUpload>> jobs_to_flush;
	{
		std::lock_guard<std::mutex> lock(m_upload_mutex);
		if (m_upload_staging.empty()) {
			return;
		}
		// Move the contents to local list and clear the shared one
		jobs_to_flush = std::move(m_upload_staging);
		m_upload_staging.clear();
	}

	auto& device = m_core->getDevice();
	auto& transferCmd = m_frames[m_current_frame].transfer_command_buffer;

	transferCmd.reset();
	transferCmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	for (auto& job : jobs_to_flush) {
		job->record(*transferCmd);
	}

	transferCmd.end();

	// Create a single fence for the entire batch group
	BatchedUploadGroup batch;
	batch.completionFence = vk::raii::Fence(device, vk::FenceCreateInfo {});
	batch.jobs = std::move(jobs_to_flush);    // Move local list into the batch tracker

	const vk::CommandBuffer rawTransferCmd = *transferCmd;
	const vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &rawTransferCmd);
	m_core->getTransferQueue().submit(submitInfo, *batch.completionFence);

	m_pending_uploads.push(std::move(batch));
}

void VulkanRenderer::setActiveCamera(Camera* camera) {
	m_camera = camera;
}
}
