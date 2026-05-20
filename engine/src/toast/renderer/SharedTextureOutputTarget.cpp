/// @file SharedTextureOutputTarget.cpp
/// @author dario
/// @date 16/05/2026.

#include "SharedTextureOutputTarget.hpp"

#include <stdexcept>

namespace toast::renderer {

SharedTextureOutputTarget::SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferredExtent, uint32_t imageCount)
    : m_core(&core),
      m_extent(preferredExtent),
      m_images(imageCount),
      m_imageViews(imageCount) {
	if (imageCount == 0) {
		throw std::runtime_error("Toast Engine Error: Shared texture output target needs at least one image!");
	}
}

const vk::raii::ImageView& SharedTextureOutputTarget::getColorAttachment(uint32_t index) const {
	return *m_imageViews.at(index);
}

vk::ResultValue<uint32_t> SharedTextureOutputTarget::acquireNextImage(uint64_t, vk::Semaphore, vk::Fence) {
	uint32_t imageIndex = m_nextAcquireIndex;
	m_nextAcquireIndex = (m_nextAcquireIndex + 1) % getImageCount();
	return vk::ResultValue<uint32_t>(vk::Result::eSuccess, std::move(imageIndex));
}

vk::Result SharedTextureOutputTarget::present(uint32_t, vk::Semaphore) {
	return vk::Result::eSuccess;
}

void SharedTextureOutputTarget::recreate(vk::Extent2D extent) {
	m_extent = extent;
}

void SharedTextureOutputTarget::setExportedImages(std::vector<ExportedImage> images) {
	m_images = std::move(images);
	m_imageViews.clear();
	m_imageViews.resize(m_images.size());
}

}
