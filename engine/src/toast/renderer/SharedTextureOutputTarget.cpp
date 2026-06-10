/// @file SharedTextureOutputTarget.cpp
/// @author dario
/// @date 16/05/2026.

#include "SharedTextureOutputTarget.hpp"

#include "VulkanCore.hpp"
#include "toast/log.hpp"

#include <cstring>

namespace toast::renderer {

namespace {

auto colorSubresourceRange() -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
}

}

SharedTextureOutputTarget::SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferredExtent, uint32_t imageCount)
    : m_core(&core),
      m_extent(preferredExtent),
      m_images(imageCount) {
	if (imageCount == 0) {
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

	for (auto& shared : m_images) {
		// Reset any previous resources first
		shared.mapped = nullptr;
		shared.view.reset();
		shared.staging.reset();
		shared.image.reset();

		// Device-local image we render into and copy out of
		vk::ImageCreateInfo image_ci {};
		image_ci.imageType = vk::ImageType::e2D;
		image_ci.format = m_colorFormat;
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

		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **shared.image;
		view_ci.viewType = vk::ImageViewType::e2D;
		view_ci.format = m_colorFormat;
		view_ci.subresourceRange = colorSubresourceRange();
		shared.view.emplace(m_core->getDevice(), view_ci);

		// stagingg buffer for CPU readback
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = staging_size;
		buffer_ci.usage = vk::BufferUsageFlagBits::eTransferDst;
		buffer_ci.sharingMode = vk::SharingMode::eExclusive;

		vma::AllocationCreateInfo buffer_alloc_ci {};
		buffer_alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		buffer_alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom;

		shared.staging.emplace(m_core->getAllocator().createBuffer(buffer_ci, buffer_alloc_ci));
		shared.mapped = shared.staging->getAllocation().getInfo().pMappedData;
	}

	TOAST_TRACE("SharedTextureOutput", "Allocated {} shared images at {}x{}", m_images.size(), m_extent.width, m_extent.height);
}

const vk::Image& SharedTextureOutputTarget::getColorImage(uint32_t index) const {
	return **m_images.at(index).image;
}

const vk::raii::ImageView& SharedTextureOutputTarget::getColorAttachment(uint32_t index) const {
	return *m_images.at(index).view;
}

vk::ResultValue<uint32_t> SharedTextureOutputTarget::acquireNextImage(uint64_t, vk::Semaphore, vk::Fence) {
	uint32_t imageIndex = m_nextAcquireIndex;
	m_nextAcquireIndex = (m_nextAcquireIndex + 1) % getImageCount();
	return vk::ResultValue<uint32_t>(vk::Result::eSuccess, std::move(imageIndex));
}

vk::Result SharedTextureOutputTarget::present(uint32_t, vk::Semaphore) {
	return vk::Result::eSuccess;
}

void SharedTextureOutputTarget::recordFinalize(vk::CommandBuffer commandBuffer, uint32_t imageIndex) {
	const auto& shared = m_images.at(imageIndex);
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
	commandBuffer.pipelineBarrier(
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
	commandBuffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, staging, region);

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
	commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, {}, to_host, {});
}

void SharedTextureOutputTarget::onImageRenderComplete(uint32_t imageIndex) {
	if (imageIndex >= m_images.size()) {
		return;
	}
	// The GPU copy into this staging buffer is complete
	// invalidate caches and publish it
	auto& shared = m_images[imageIndex];
	if (shared.staging.has_value()) {
		shared.staging->getAllocation().invalidate(0, imageByteSize());
	}
	m_latestReady.store(static_cast<int32_t>(imageIndex), std::memory_order_release);
	m_frameCounter.fetch_add(1, std::memory_order_acq_rel);
}

int SharedTextureOutputTarget::copyLatestFrame(void* dst, uint32_t dstCapacity, ViewportFrameDesc* out) {
	std::scoped_lock lock(m_frameMutex);

	const int32_t index = m_latestReady.load(std::memory_order_acquire);
	if (index < 0 || static_cast<size_t>(index) >= m_images.size()) {
		return 0;
	}

	const uint32_t row_pitch = m_extent.width * 4;
	const uint32_t required = row_pitch * m_extent.height;
	if (out) {
		out->width = m_extent.width;
		out->height = m_extent.height;
		out->row_pitch = row_pitch;
		out->frame_id = m_frameCounter.load(std::memory_order_acquire);
	}

	const void* src = m_images[static_cast<size_t>(index)].mapped;
	if (dst == nullptr || src == nullptr || dstCapacity < required) {
		return -1;
	}

	std::memcpy(dst, src, required);
	return 1;
}

void SharedTextureOutputTarget::recreate(vk::Extent2D extent) {
	std::scoped_lock lock(m_frameMutex);
	m_latestReady.store(-1, std::memory_order_release);
	m_nextAcquireIndex = 0;
	allocateResources(extent);
}

}
