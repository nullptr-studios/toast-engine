/// @file SharedTextureOutputTarget.cpp
/// @author dario
/// @date 16/05/2026.

#include "shared_texture_output_target.hpp"

#include "toast/log.hpp"
#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <cstring>
#include <format>

namespace renderer {

namespace {

auto colorSubresourceRange() -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
}

}

SharedTextureOutputTarget::SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferred_extent, uint32_t image_count)
    : m_core(&core),
      m_extent(preferred_extent),
      m_images(image_count) {
	if (image_count == 0) {
		TOAST_CRITICAL("SharedTextureOutput", "Toast Engine Error: Shared texture output target needs at least one image!");
	}
	allocateResources(m_extent);
}

void SharedTextureOutputTarget::allocateResources(vk::Extent2D extent) {
	m_extent = extent;
	if (m_extent.width == 0 || m_extent.height == 0) {
		TOAST_CRITICAL("SharedTextureOutput", "Toast Engine Error: Shared texture output target needs a non-zero extent!");
	}

	const vk::DeviceSize staging_size = imageByteSize();

	for (uint32_t index = 0; index < m_images.size(); ++index) {
		auto& shared = m_images[index];
		// Reset any previous resources first
		shared.mapped = nullptr;
		shared.view.reset();
		shared.staging.reset();
		shared.image.reset();

		// Device-local image we render into and copy out of
		vk::ImageCreateInfo image_ci {};
		image_ci.imageType = vk::ImageType::e2D;
		image_ci.format = m_color_format;
		image_ci.extent = vk::Extent3D {m_extent.width, m_extent.height, 1};
		image_ci.mipLevels = 1;
		image_ci.arrayLayers = 1;
		image_ci.samples = vk::SampleCountFlagBits::e1;
		image_ci.tiling = vk::ImageTiling::eOptimal;
		image_ci.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
		image_ci.sharingMode = vk::SharingMode::eExclusive;
		image_ci.initialLayout = vk::ImageLayout::eUndefined;

		vma::AllocationCreateInfo image_alloc_ci {};
		image_alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

		shared.image.emplace(m_core->getAllocator().createImage(image_ci, image_alloc_ci));
		setDebugName(*m_core, **shared.image, std::format("SharedTextureOutput Image[{}]", index));

		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **shared.image;
		view_ci.viewType = vk::ImageViewType::e2D;
		view_ci.format = m_color_format;
		view_ci.subresourceRange = colorSubresourceRange();
		shared.view.emplace(m_core->getDevice(), view_ci);
		setDebugName(*m_core, **shared.view, std::format("SharedTextureOutput ImageView[{}]", index));

		// stagingg buffer for CPU readback
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = staging_size;
		buffer_ci.usage = vk::BufferUsageFlagBits::eTransferDst;
		buffer_ci.sharingMode = vk::SharingMode::eExclusive;

		vma::AllocationCreateInfo buffer_alloc_ci {};
		buffer_alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		buffer_alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom;

		shared.staging.emplace(m_core->getAllocator().createBuffer(buffer_ci, buffer_alloc_ci));
		setDebugName(*m_core, **shared.staging, std::format("SharedTextureOutput StagingBuffer[{}]", index));
		shared.mapped = shared.staging->getAllocation().getInfo().pMappedData;
	}

	TOAST_TRACE("SharedTextureOutput", "Allocated {} shared images at {}x{}", m_images.size(), m_extent.width, m_extent.height);
}

auto SharedTextureOutputTarget::getColorImage(uint32_t index) const -> const vk::Image& {
	return **m_images.at(index).image;
}

auto SharedTextureOutputTarget::getColorAttachment(uint32_t index) const -> const vk::raii::ImageView& {
	return *m_images.at(index).view;
}

auto SharedTextureOutputTarget::acquireNextImage(uint64_t, vk::Semaphore, vk::Fence) -> vk::ResultValue<uint32_t> {
	uint32_t image_index = m_next_acquire_index;
	m_next_acquire_index = (m_next_acquire_index + 1) % getImageCount();
	return {vk::Result::eSuccess, image_index};
}

auto SharedTextureOutputTarget::present(uint32_t, vk::Semaphore) -> vk::Result {
	return vk::Result::eSuccess;
}

void SharedTextureOutputTarget::recordFinalize(vk::CommandBuffer command_buffer, uint32_t image_index) {
	const auto& shared = m_images.at(image_index);
	const vk::Image image = **shared.image;
	const vk::Buffer staging = **shared.staging;

	// Transition the rendered image from color-attachment to transfer-source
	const vk::ImageMemoryBarrier to_transfer(
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    vk::AccessFlagBits::eTransferRead,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    vk::ImageLayout::eTransferSrcOptimal,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    image,
	    colorSubresourceRange()
	);
	command_buffer.pipelineBarrier(
	    vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, to_transfer
	);

	// Copy the image into its host-visible staging buffer
	const vk::BufferImageCopy region(
	    0,
	    0,
	    0,
	    vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
	    vk::Offset3D {0, 0, 0},
	    vk::Extent3D {m_extent.width, m_extent.height, 1}
	);
	command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, staging, region);

	// Make the transfer write visible to host reads
	const vk::BufferMemoryBarrier to_host(
	    vk::AccessFlagBits::eTransferWrite,
	    vk::AccessFlagBits::eHostRead,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    staging,
	    0,
	    VK_WHOLE_SIZE
	);
	command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, {}, to_host, {});
}

void SharedTextureOutputTarget::onImageRenderComplete(uint32_t image_index) {
	if (image_index >= m_images.size()) {
		return;
	}
	// The GPU copy into this staging buffer is complete
	// invalidate caches and publish it
	auto& shared = m_images[image_index];
	if (shared.staging.has_value()) {
		shared.staging->getAllocation().invalidate(0, imageByteSize());
	}
	m_latest_ready.store(static_cast<int32_t>(image_index), std::memory_order_release);
	m_frame_counter.fetch_add(1, std::memory_order_acq_rel);
}

auto SharedTextureOutputTarget::copyLatestFrame(void* dst, uint32_t dst_capacity, ViewportFrameDesc* out) -> int {
	std::scoped_lock lock(m_frame_mutex);

	const int32_t index = m_latest_ready.load(std::memory_order_acquire);
	if (index < 0 || static_cast<size_t>(index) >= m_images.size()) {
		return 0;
	}

	const uint32_t row_pitch = m_extent.width * 4;
	const uint32_t required = row_pitch * m_extent.height;
	if (out) {
		out->width = m_extent.width;
		out->height = m_extent.height;
		out->row_pitch = row_pitch;
		out->frame_id = m_frame_counter.load(std::memory_order_acquire);
	}

	const void* src = m_images[static_cast<size_t>(index)].mapped;
	if (dst == nullptr || src == nullptr || dst_capacity < required) {
		return -1;
	}

	std::memcpy(dst, src, required);
	return 1;
}

void SharedTextureOutputTarget::recreate(vk::Extent2D extent) {
	std::scoped_lock lock(m_frame_mutex);
	m_latest_ready.store(-1, std::memory_order_release);
	m_next_acquire_index = 0;
	allocateResources(extent);
}

}
