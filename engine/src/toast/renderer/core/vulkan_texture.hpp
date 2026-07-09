/// @file vulkan_texture.hpp
/// @author dario
/// @date 6/28/2026.

#pragma once
#include "ktx.h"
#include "vulkan_common.hpp"
#include "vulkan_resource_base.hpp"

#include <string>
#include <string_view>

namespace toast::renderer {

class VulkanTexture : public IVulkanResource {
public:
	VulkanTexture() = default;

	struct Params {
		vk::Format format;
		vk::Extent3D extent;
		uint32_t mip_levels = 1;
		uint32_t layer_count = 1;
		bool is_cubemap = false;
	};
	
	
	void create(const VulkanCore& core, Params params, std::string_view debug_name = {});
	void destroy();

	[[nodiscard]]
	auto getImage() const -> vk::Image {
		return **m_image;
	}

	[[nodiscard]]
	auto getView() const -> vk::ImageView {
		return *m_image_view;
	}

private:
	std::optional<vma::raii::Image> m_image;
	vk::raii::ImageView m_image_view = nullptr;
	Params m_params;
};

// Upload Resource
class TextureUpload : public PendingResourceUpload {
public:
	TextureUpload(VulkanTexture& texture, const std::vector<uint8_t>& data, std::string_view debug_name = {})
	    : m_texture(&texture),
	      m_data(data),
	      m_debug_name(debug_name) { }

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
	std::string m_debug_name;

	ktxTexture2* m_ktxTexture = nullptr;
	vma::raii::Buffer m_stagingBuffer = nullptr;
	std::vector<vk::BufferImageCopy> m_copyRegions;
	VulkanTexture::Params m_texParams {};
};

class RawTextureUpload : public PendingResourceUpload {
public:
	RawTextureUpload(
	    VulkanTexture& texture, std::vector<uint8_t> data, uint32_t width, uint32_t height, vk::Format format,
	    std::string_view debug_name = {}
	)
	    : m_texture(&texture),
	      m_data(std::move(data)),
	      m_width(width),
	      m_height(height),
	      m_format(format),
	      m_debug_name(debug_name) { }

	void build(const VulkanCore& core) override;
	void record(vk::CommandBuffer cmd) override;

	IVulkanResource* resource() override { return m_texture; }

private:
	VulkanTexture* m_texture = nullptr;
	std::vector<uint8_t> m_data;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	vk::Format m_format = vk::Format::eUndefined;
	std::string m_debug_name;
	vma::raii::Buffer m_staging_buffer = nullptr;
};

}
