/// @file vulkan_texture.hpp
/// @author dario
/// @date 6/28/2026.

#pragma once
#include "ktx.h"
#include "vulkan_common.hpp"
#include "vulkan_resource_base.hpp"

namespace toast::renderer {

class VulkanTexture : public IVulkanResource {
public:
	VulkanTexture() = default;

	struct Params {
		vk::Format format;
		vk::Extent3D extent;
		uint32_t mipLevels = 1;
		uint32_t layerCount = 1;
		bool isCubemap = false;
	};

	void create(const VulkanCore& core, Params params);
	void destroy();

	vk::Image getImage() const { return **m_image; }

	vk::ImageView getView() const { return *m_imageView; }

private:
	std::optional<vma::raii::Image> m_image;
	vk::raii::ImageView m_imageView = nullptr;
	Params m_params;
};

// Upload Resource
class TextureUpload : public PendingResourceUpload {
public:
	TextureUpload(VulkanTexture& texture, const std::vector<uint8_t>& data) : m_texture(&texture), m_data(data) { }

	~TextureUpload() override {
		if (m_ktxTexture) {
			ktxTexture2_Destroy(m_ktxTexture);
		}
	}

	void build(const VulkanCore& core) override;
	void record(vk::CommandBuffer cmd) override;

	IVulkanResource* resource() override { return m_texture; }

private:
	VulkanTexture* m_texture;
	std::vector<uint8_t> m_data;

	ktxTexture2* m_ktxTexture = nullptr;
	vma::raii::Buffer m_stagingBuffer = nullptr;
	std::vector<vk::BufferImageCopy> m_copyRegions;
	VulkanTexture::Params m_texParams;
};

}
