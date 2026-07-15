/// @file VulkanRenderer.cpp
/// @author dario
/// @date 16/05/2026.

#include "vulkan_renderer.hpp"

#include "toast/log.hpp"
#include "toast/thread_pool.hpp"
#include "toast/time.hpp"
#include "toast/window/window_events.hpp"
#include "toast/world/camera.hpp"
#include "toast/world/mesh_node.hpp"
#include "vulkan_debug.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
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

VulkanRenderer::VulkanRenderer(const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target) noexcept
    : m_core(&core),
      m_output_target(std::move(output_target)) {
	instance = this;
	if (!m_output_target) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires an output target!");
	}

	if (k_frames_in_flight == 0) {
		TOAST_CRITICAL("VulkanRenderer", "Toast Engine Error: VulkanRenderer requires at least one frame in flight!");
	}

	TOAST_TRACE("VulkanRenderer", "Creating renderer with {} frame(s) in flight", k_frames_in_flight);
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
	setDebugName(*m_core, *m_command_pool, "VulkanRenderer GraphicsCommandPool");
	TOAST_TRACE("VulkanRenderer", "Graphics command pool created (graphics family {})", m_core->getGraphicsQueueFamilyIndex());
}

auto VulkanRenderer::createTransferCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getTransferQueueFamilyIndex()
	);
	m_transfer_command_pool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	setDebugName(*m_core, *m_transfer_command_pool, "VulkanRenderer TransferCommandPool");
	TOAST_TRACE("VulkanRenderer", "Transfer command pool created (transfer family {})", m_core->getTransferQueueFamilyIndex());
}

auto VulkanRenderer::createFrameContexts() -> void {
	m_frames.clear();
	m_frames.resize(k_frames_in_flight);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	const vk::FenceCreateInfo fence_ci(vk::FenceCreateFlagBits::eSignaled);
	const vk::CommandBufferAllocateInfo command_buffer_ci(*m_command_pool, vk::CommandBufferLevel::ePrimary, k_frames_in_flight);
	const vk::CommandBufferAllocateInfo transfer_command_buffer_ci(
	    *m_transfer_command_pool, vk::CommandBufferLevel::ePrimary, k_frames_in_flight
	);
	auto allocated_command_buffers = m_core->getDevice().allocateCommandBuffers(command_buffer_ci);
	auto allocated_transfer_command_buffers = m_core->getDevice().allocateCommandBuffers(transfer_command_buffer_ci);

	for (uint32_t frame_index = 0; frame_index < k_frames_in_flight; ++frame_index) {
		m_frames[frame_index].command_buffer = std::move(allocated_command_buffers[frame_index]);
		m_frames[frame_index].transfer_command_buffer = std::move(allocated_transfer_command_buffers[frame_index]);
		m_frames[frame_index].image_available = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].transfer_finished = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].in_flight = vk::raii::Fence(m_core->getDevice(), fence_ci);

		setDebugName(
		    *m_core, *m_frames[frame_index].command_buffer, std::format("VulkanRenderer Frame[{}] CommandBuffer", frame_index)
		);
		setDebugName(
		    *m_core,
		    *m_frames[frame_index].transfer_command_buffer,
		    std::format("VulkanRenderer Frame[{}] TransferCommandBuffer", frame_index)
		);
		setDebugName(
		    *m_core, *m_frames[frame_index].image_available, std::format("VulkanRenderer Frame[{}] ImageAvailable", frame_index)
		);
		setDebugName(
		    *m_core, *m_frames[frame_index].transfer_finished, std::format("VulkanRenderer Frame[{}] TransferFinished", frame_index)
		);
		setDebugName(*m_core, *m_frames[frame_index].in_flight, std::format("VulkanRenderer Frame[{}] InFlightFence", frame_index));
	}

	TOAST_TRACE("VulkanRenderer", "Frame command buffers created: {}", k_frames_in_flight);
}

auto VulkanRenderer::createPerImageSync() -> void {
	const auto image_count = m_output_target->getImageCount();
	m_render_finished_per_image.clear();
	m_render_finished_per_image.reserve(image_count);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	for (uint32_t i = 0; i < image_count; ++i) {
		m_render_finished_per_image.emplace_back(m_core->getDevice(), semaphore_ci);
		setDebugName(*m_core, *m_render_finished_per_image.back(), std::format("VulkanRenderer RenderFinished[{}]", i));
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
	setDebugName(*m_core, **m_depth_resources.image, "VulkanRenderer DepthImage");

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_depth_resources.image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = m_depth_format;
	view_ci.subresourceRange = depthAttachmentRange(m_depth_format);
	m_depth_resources.view.emplace(m_core->getDevice(), view_ci);
	setDebugName(*m_core, **m_depth_resources.view, "VulkanRenderer DepthImageView");

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
	std::array pool_sizes {
	  vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1024),

	  vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4096),

	  vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1024)
	};
	vk::DescriptorPoolCreateInfo pool_ci {};
	pool_ci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

	pool_ci.maxSets = 8192;

	pool_ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());

	pool_ci.pPoolSizes = pool_sizes.data();

	m_descriptor_pool = vk::raii::DescriptorPool(m_core->getDevice(), pool_ci);
	setDebugName(*m_core, *m_descriptor_pool, "VulkanRenderer DescriptorPool");
}

auto VulkanRenderer::recordFrame(FrameContext& frame, uint32_t image_index) noexcept -> void {
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
	// if (m_frameUniformResources.descriptor_set != VK_NULL_HANDLE) {
	// 	const std::array<vk::DescriptorSet, 1> descriptor_sets {m_frameUniformResources.descriptor_set};
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
	m_frame_ubo_res.resize(k_frames_in_flight);
	m_frame_ubos.resize(k_frames_in_flight);

	const auto& device = m_core->getDevice();

	const vk::DeviceSize buffer_size = sizeof(FrameUBO);

	// Create per-frame UBO buffers only. Descriptor sets are allocated by individual render passes
	for (uint32_t i = 0; i < m_frame_ubo_res.size(); ++i) {
		auto& frame = m_frame_ubo_res[i];

		// create ubo
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = buffer_size;
		buffer_ci.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		// allocation
		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;

		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		// create buffer
		frame.gpu_buffer.emplace(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(*m_core, **frame.gpu_buffer, std::format("VulkanRenderer FrameUBO[{}]", i));

		// no staging buffer

		// Leave descriptor_set empty render passes will allocate and manage their own descriptor sets
	}
}

void VulkanRenderer::updateFrameResources(uint32_t frame_index, RenderFrame& frame_data) {
	m_frame_ubos[frame_index] = frame_data.frame_data;

	const auto& allocation = m_frame_ubo_res[frame_index].gpu_buffer->getAllocation();
	// With VMA_ALLOCATION_CREATE_MAPPED
	auto* mapped = allocation.getInfo().pMappedData;

	if (mapped) {
		std::memcpy(mapped, &m_frame_ubos[frame_index], sizeof(FrameUBO));

		allocation.flush(0, sizeof(FrameUBO));
	}
}

auto VulkanRenderer::drawFrame(RenderFrame& frame_data) -> void {
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
	updateFrameResources(m_current_frame, frame_data);    // FIXME: dt

	m_rendering_frame = &frame_data;

	// Update the Render passes TODO: Move outside of renderloop
	for (auto& pass : m_render_passes) {
		pass->update(m_current_frame, Time::renderDelta());
	}

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

		// Only a real present actually waits on this. Off-screen output targets
		// ignore the semaphore IOutputTarget::present() is handed
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &signal_semaphore;
	}
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	m_core->getGraphicsQueue().submit(submit_info, *frame.in_flight);

	// Present onto target texture
	const auto present_result = m_output_target->present(image_index, present_sync ? signal_semaphore : vk::Semaphore {});
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

	using clock = std::chrono::steady_clock;
	auto next_frame_deadline = clock::now();

	while (m_running.load(std::memory_order_acquire)) {
		ZoneScopedN("VulkanRenderer::mainRenderThread");

		const uint64_t pending_resize = m_pending_resize_packed.exchange(k_no_pending_resize, std::memory_order_acq_rel);
		if (pending_resize != k_no_pending_resize) {
			const auto extent = unpackExtent(pending_resize);
			if (extent.width > 0 && extent.height > 0) {
				applyResizeInternal(extent);
			}
		}

		RenderFrame frame_to_draw;
		bool consumed_queued_frame = false;
		const double limit_hz = m_frame_rate_limit_hz.load(std::memory_order_relaxed);

		{
			std::unique_lock lock(m_queue_mutex);

			auto wake_condition = [this] {
				return !m_ready_frames.empty() || !m_running ||
				       m_pending_resize_packed.load(std::memory_order_acquire) != k_no_pending_resize;
			};

			if (m_ready_frames.empty()) {
				if (!m_has_cached_frame) {
					// Nothing has ever been drawn yet, so block indefinitely for the first frame
					m_frame_cv.wait(lock, wake_condition);
				} else if (limit_hz > 0.0) {
					// Capped, wait till new frame arrives
					const auto now = clock::now();
					if (next_frame_deadline > now) {
						m_frame_cv.wait_for(lock, next_frame_deadline - now, wake_condition);
					}
				}
				// Uncapped, draws even if no new frame data
			}

			if (!m_running) {
				return;
			}

			if (!m_ready_frames.empty()) {
				const auto frame_index = m_ready_frames.front();
				m_ready_frames.pop();
				frame_to_draw = m_render_frames[frame_index];
				m_cached_frame = frame_to_draw;
				m_has_cached_frame = true;
				consumed_queued_frame = true;
			} else if (m_has_cached_frame) {
				frame_to_draw = m_cached_frame;
			} else {
				continue;
			}
		}

		if (limit_hz > 0.0) {
			// Pace every draw uniformly, so a burst of frames from the main thread can't exceed the cap either
			std::this_thread::sleep_until(next_frame_deadline);
			const auto interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / limit_hz));
			const auto now = clock::now();
			// If we're already behind the natural next deadline, resync to "now +
			// interval" instead of stacking missed deadlines
			next_frame_deadline = (next_frame_deadline + interval > now) ? next_frame_deadline + interval : now + interval;
		}

		Time::get().renderTick();

		// TODO: Make this a editor keybind
		// m_core->getRenderDocAPI()->StartFrameCapture(nullptr, nullptr);

		drawFrame(frame_to_draw);

		// m_core->getRenderDocAPI()->EndFrameCapture(nullptr, nullptr);

		if (consumed_queued_frame) {
			m_free_frames.release();
		}
	}
}

void VulkanRenderer::start() noexcept {
	TOAST_TRACE("VulkanRenderer", "Starting renderer");

	m_running.store(true, std::memory_order_release);

	m_render_thread = std::thread([this] { mainRenderThread(); });
}

void VulkanRenderer::submitFrame() noexcept {
	// TracyMessage("Swapped frame resource", 256);
	{
		std::lock_guard lock(m_queue_mutex);

		m_ready_frames.push(m_write_index);

		m_write_index = (m_write_index + 1) % k_render_frames;
	}

	m_frame_cv.notify_one();
}

void VulkanRenderer::tick(float time) noexcept {
	ZoneScopedN("VulkanRenderer::tick()");

	// Dont block the main thread if theres no free render slot
	if (!m_free_frames.try_acquire()) {
		return;
	}

	auto& frame = beginFrameBuild();

	frame.mesh_instances.clear();
	frame.debug_line_vertices.clear();
	frame.debug_gizmo_instances.clear();

	std::vector<MeshNode*> mesh_nodes_snapshot;
	{
		std::scoped_lock lock(m_mesh_proxy_mutex);
		mesh_nodes_snapshot = m_mesh_proxy_nodes;
	}
	frame.mesh_instances.reserve(mesh_nodes_snapshot.size());

	for (auto* node : mesh_nodes_snapshot) {
		if (node == nullptr || !node->enabled()) {
			continue;
		}

		auto& mesh_handle = node->getMesh();
		if (!mesh_handle.hasValue()) {
			continue;
		}

		auto& gpu_mesh = mesh_handle->gpuMesh();
		if (!gpu_mesh.isReady()) {
			continue;
		}

		auto& material_handle = node->getMaterial();
		if (material_handle.hasValue()) {
			material_handle->resolveTextureHandles();
		}

		const auto world_transform = node->worldTransformForRender();

		frame.mesh_instances.push_back(
		    MeshInstanceProxy {
		      .mesh = &gpu_mesh,
		      .material = material_handle.hasValue() ? &material_handle.get() : nullptr,
		      .model = world_transform,
		    }
		);

		// DEBUG
		frame.debug_gizmo_instances.push_back(world_transform);
	}

	if (m_camera) {
		const auto extent = m_output_target->getExtent();
		const float aspect =
		    extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : (1080.0f / 720.0f);

		frame.frame_data = FrameUBO {
		  .view = m_camera->getView(),
		  .projection = m_camera->getProjection(aspect),
		  .view_projection = m_camera->getProjection(aspect) * m_camera->getView(),
		  .camera_position = m_camera->worldPos(),
		  .time = time
		};
	}

	submitFrame();
}

void VulkanRenderer::registerMeshNodeProxy(MeshNode* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_mesh_proxy_mutex);
	if (!std::ranges::contains(m_mesh_proxy_nodes, node)) {
		m_mesh_proxy_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterMeshNodeProxy(MeshNode* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_mesh_proxy_mutex);
	std::erase(m_mesh_proxy_nodes, node);
}

void VulkanRenderer::stop() {
	const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
	if (!was_running) {
		return;
	}

	m_frame_cv.notify_all();

	if (m_render_thread.joinable()) {
		m_render_thread.join();
	}

	// queueResourceUpload() dispatches PendingResourceUpload::build() to the thread pool
	while (m_pending_upload_builds.load(std::memory_order_acquire) > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	if (m_core) {
		m_core->getDevice().waitIdle();
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

void VulkanRenderer::queueResourceUpload(std::unique_ptr<PendingResourceUpload> upload_job) {
	// build() can be expensive so its dispatched to the thread pool to avoid blocking the main thread
	m_pending_upload_builds.fetch_add(1, std::memory_order_relaxed);
	toast::ThreadPool::push([this, job = std::move(upload_job)]() mutable {
		job->build(*m_core);

		{
			std::lock_guard<std::mutex> lock(m_upload_mutex);
			m_upload_staging.push_back(std::move(job));
		}
		m_pending_upload_builds.fetch_sub(1, std::memory_order_acq_rel);
	});
}

void VulkanRenderer::processPendingUploads() {
	const auto& device = m_core->getDevice();

	while (!m_pending_uploads.empty()) {
		auto& oldest_batch = m_pending_uploads.front();

		const auto status = vkGetFenceStatus(*device, *oldest_batch.completion_fence);

		if (status == VkResult::VK_SUCCESS) {
			for (auto& job : oldest_batch.jobs) {
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

	const auto& device = m_core->getDevice();
	auto& transfer_cmd = m_frames[m_current_frame].transfer_command_buffer;

	transfer_cmd.reset();
	transfer_cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	for (auto& job : jobs_to_flush) {
		job->record(*transfer_cmd);
	}

	transfer_cmd.end();

	// Create a single fence for the entire batch group
	BatchedUploadGroup batch;
	batch.completion_fence = vk::raii::Fence(device, vk::FenceCreateInfo {});
	batch.jobs = std::move(jobs_to_flush);    // Move local list into the batch tracker

	const vk::CommandBuffer raw_transfer_cmd = *transfer_cmd;
	const vk::SubmitInfo submit_info(0, nullptr, nullptr, 1, &raw_transfer_cmd);
	m_core->getTransferQueue().submit(submit_info, *batch.completion_fence);

	m_pending_uploads.push(std::move(batch));
}

void VulkanRenderer::setActiveCamera(Camera* camera) {
	m_camera = camera;
}

void debugDrawFrustum(const toast::Camera& camera, float aspect, glm::vec4 color) {
	const float tan_half_fov_y = std::tan(glm::radians(camera.fov) * 0.5f);
	const float near_height = 2.0f * tan_half_fov_y * camera.near_plane;
	const float near_width = near_height * aspect;
	const float far_height = 2.0f * tan_half_fov_y * camera.far_plane;
	const float far_width = far_height * aspect;

	const std::array<glm::vec3, 8> view_space_corners {
	  glm::vec3 {-near_width * 0.5f, -near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { near_width * 0.5f, -near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { near_width * 0.5f,  near_height * 0.5f, -camera.near_plane},
	  glm::vec3 {-near_width * 0.5f,  near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { -far_width * 0.5f,  -far_height * 0.5f,  -camera.far_plane},
	  glm::vec3 {  far_width * 0.5f,  -far_height * 0.5f,  -camera.far_plane},
	  glm::vec3 {  far_width * 0.5f,   far_height * 0.5f,  -camera.far_plane},
	  glm::vec3 { -far_width * 0.5f,   far_height * 0.5f,  -camera.far_plane},
	};

	const glm::mat4 inv_view = glm::inverse(camera.getView());
	std::array<glm::vec3, 8> world_corners {};
	for (int i = 0; i < 8; ++i) {
		world_corners[i] = glm::vec3(inv_view * glm::vec4(view_space_corners[i], 1.0f));
	}

	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		debugDrawLine(world_corners[a], world_corners[b], color);
	}
}

}
