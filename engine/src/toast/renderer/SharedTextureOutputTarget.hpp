/// @file SharedTextureOutputTarget.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "IOutputTarget.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace toast::renderer {

class VulkanCore;

class SharedTextureOutputTarget final : public IOutputTarget {
public:
	struct ExportedImage {
		vk::Image image = VK_NULL_HANDLE;
		uint64_t exportedHandle = 0;
	};

	SharedTextureOutputTarget(const VulkanCore& core, vk::Extent2D preferredExtent, uint32_t imageCount = 3);
	~SharedTextureOutputTarget() override = default;

	[[nodiscard]]
	vk::Extent2D getExtent() const override {
		return m_extent;
	}

	[[nodiscard]]
	vk::Format getColorFormat() const override {
		return m_colorFormat;
	}

	[[nodiscard]]
	uint32_t getImageCount() const override {
		return static_cast<uint32_t>(m_images.size());
	}

	[[nodiscard]]
	const vk::Image& getColorImage(uint32_t index) const override {
		return m_images.at(index).image;
	}

	[[nodiscard]]
	const vk::raii::ImageView& getColorAttachment(uint32_t index) const override;
	[[nodiscard]]
	vk::ResultValue<uint32_t> acquireNextImage(uint64_t timeout, vk::Semaphore imageAvailable, vk::Fence inFlightFence) override;
	[[nodiscard]]
	vk::Result present(uint32_t imageIndex, vk::Semaphore renderFinished) override;

	void recreate(vk::Extent2D extent) override;

	void setExportedImages(std::vector<ExportedImage> images);

	[[nodiscard]]
	const std::vector<ExportedImage>& getExportedImages() const {
		return m_images;
	}

private:
	const VulkanCore* m_core = nullptr;
	vk::Extent2D m_extent {};
	vk::Format m_colorFormat = vk::Format::eB8G8R8A8Unorm;
	std::vector<ExportedImage> m_images;
	std::vector<std::optional<vk::raii::ImageView>> m_imageViews;
	uint32_t m_nextAcquireIndex = 0;
};

}
