/// @file vulkan_texture.cpp
/// @author dario
/// @date 6/28/2026.

#include "vulkan_texture.hpp"

#include "toast/log.hpp"
#include "vulkan_debug.hpp"

#include <cstring>
#include <format>

namespace renderer {

void VulkanTexture::create(const VulkanCore& core, Params params, std::string_view debug_name) {
	m_params = params;

	const std::string name =
	    debug_name.empty()
	        ? std::format("Texture {}x{} {}", params.extent.width, params.extent.height, vk::to_string(params.format))
	        : std::string(debug_name);

	vk::ImageCreateInfo image_ci {};
	image_ci.format = m_params.format;
	image_ci.extent = m_params.extent;
	image_ci.mipLevels = m_params.mip_levels;
	image_ci.arrayLayers = m_params.layer_count;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	if (m_params.is_cubemap) {
		image_ci.flags |= vk::ImageCreateFlagBits::eCubeCompatible;
	}

	if (m_params.extent.depth > 1) {
		image_ci.imageType = vk::ImageType::e3D;
	} else {
		image_ci.imageType = vk::ImageType::e2D;
	}

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_image.emplace(core.getAllocator().createImage(image_ci, alloc_ci));
	setDebugName(core, **m_image, name);

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_image;

	if (m_params.extent.depth > 1) {
		view_ci.viewType = vk::ImageViewType::e3D;
	} else if (m_params.is_cubemap) {
		// A  cubemap has 6 layers
		view_ci.viewType = m_params.layer_count > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
	} else {
		view_ci.viewType = m_params.layer_count > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
	}

	view_ci.format = m_params.format;
	view_ci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	view_ci.subresourceRange.baseMipLevel = 0;
	view_ci.subresourceRange.levelCount = m_params.mip_levels;
	view_ci.subresourceRange.baseArrayLayer = 0;
	view_ci.subresourceRange.layerCount = m_params.layer_count;

	m_image_view = vk::raii::ImageView(core.getDevice(), view_ci);
	setDebugName(core, *m_image_view, name + " View");
}

void VulkanTexture::destroy() {
	m_image_view.clear();
	m_image.reset();
}

// Upload Functions

void TextureUpload::build(const VulkanCore& core) {
	auto result =
	    ktxTexture2_CreateFromMemory(m_data.data(), m_data.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &m_ktx_texture);
	if (result != KTX_SUCCESS) {
		TOAST_CRITICAL("TextureUpload", "Failed to open KTX image data?!");
	}

	if (ktxTexture2_NeedsTranscoding(m_ktx_texture)) {
		result = ktxTexture2_TranscodeBasis(m_ktx_texture, KTX_TTF_BC7_RGBA, 0);
		if (result != KTX_SUCCESS) {
			TOAST_CRITICAL("TextureUpload", "Failed to transcode BasisUniversal texture");
		}
	}

	m_tex_params.format = static_cast<vk::Format>(m_ktx_texture->vkFormat);

	if (m_tex_params.format == vk::Format::eR8G8B8Unorm || m_tex_params.format == vk::Format::eR8G8B8Srgb) {
		TOAST_CRITICAL("TextureUpload", "24bit format is not supported by ToastEngine, Please use RGBA format!!");
	}

	m_tex_params.extent = vk::Extent3D(m_ktx_texture->baseWidth, m_ktx_texture->baseHeight, m_ktx_texture->baseDepth);
	m_tex_params.mip_levels = std::max(1u, m_ktx_texture->numLevels);
	m_tex_params.layer_count = std::max(1u, m_ktx_texture->numLayers);

	// TODO: PROPER CUBEMAP SUPPORT
	m_tex_params.is_cubemap = m_ktx_texture->isCubemap;

	if (m_ktx_texture->isCubemap) {
		m_tex_params.layer_count = 6 * std::max(1u, m_ktx_texture->numLayers);
	}

	m_texture->create(core, m_tex_params, m_debug_name);
	m_texture->markUploading();

	const vk::DeviceSize total_size = m_ktx_texture->dataSize;

	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = total_size;
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_staging_buffer = core.getAllocator().createBuffer(staging_ci, alloc_ci);
	if (!m_debug_name.empty()) {
		setDebugName(core, *m_staging_buffer, m_debug_name + " StagingBuffer");
	}

	uint8_t* mapped_data = static_cast<uint8_t*>(m_staging_buffer.getAllocation().getInfo().pMappedData);
	std::memcpy(mapped_data, m_ktx_texture->pData, total_size);

	uint32_t num_layers = std::max(1u, m_ktx_texture->numLayers);
	uint32_t num_faces = m_ktx_texture->isCubemap ? 6 : 1;

	for (uint32_t mip = 0; mip < m_tex_params.mip_levels; ++mip) {
		for (uint32_t layer = 0; layer < num_layers; ++layer) {
			for (uint32_t face = 0; face < num_faces; ++face) {
				ktx_size_t offset = 0;

				if (ktxTexture2_GetImageOffset(m_ktx_texture, mip, layer, face, &offset) != KTX_SUCCESS) {
					continue;
				}

				vk::BufferImageCopy region {};
				region.bufferOffset = offset;
				region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				region.imageSubresource.mipLevel = mip;
				// Maps faces directly into contiguous array layers
				region.imageSubresource.baseArrayLayer = (layer * num_faces) + face;
				region.imageSubresource.layerCount = 1;

				region.imageExtent.width = std::max(1u, m_tex_params.extent.width / (1 << mip));
				region.imageExtent.height = std::max(1u, m_tex_params.extent.height / (1 << mip));
				region.imageExtent.depth = std::max(1u, m_tex_params.extent.depth / (1 << mip));

				m_copy_regions.push_back(region);
			}
		}
	}
}

void TextureUpload::record(vk::CommandBuffer cmd) {
	vk::Image image_handle = m_texture->getImage();

	vk::ImageMemoryBarrier barrier {};
	barrier.oldLayout = vk::ImageLayout::eUndefined;
	barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image_handle;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = m_tex_params.mip_levels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = m_tex_params.layer_count;
	barrier.srcAccessMask = {};
	barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

	cmd.copyBufferToImage(*m_staging_buffer, image_handle, vk::ImageLayout::eTransferDstOptimal, m_copy_regions);

	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	// This barrier is recorded on the transfer queue's command buffer
	// which doesn't support the FRAGMENT_SHADER stage - only the graphics queue does, and a barrier's stage mask
	// is scoped to the queue executing it, not the resource's eventual consumer. The layout transition to
	// eShaderReadOnlyOptimal still happens here; cross-queue visibility for the shader read itself is handled by
	// the upload's completion fence, since consumers only touch the texture once VulkanTexture::isReady() is true.
	barrier.dstAccessMask = {};

	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, nullptr, barrier
	);
}

void RawTextureUpload::build(const VulkanCore& core) {
	if (m_width == 0 || m_height == 0 || m_data.empty()) {
		TOAST_CRITICAL("RawTextureUpload", "Cannot upload raw texture with empty dimensions/data");
	}

	VulkanTexture::Params params {};
	params.format = m_format;
	params.extent = vk::Extent3D(m_width, m_height, 1);
	params.mip_levels = 1;
	params.layer_count = 1;
	params.is_cubemap = false;
	m_texture->create(core, params, m_debug_name);
	m_texture->markUploading();

	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = static_cast<vk::DeviceSize>(m_data.size());
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
	m_staging_buffer = core.getAllocator().createBuffer(staging_ci, alloc_ci);
	if (!m_debug_name.empty()) {
		setDebugName(core, *m_staging_buffer, m_debug_name + " StagingBuffer");
	}

	void* mapped = m_staging_buffer.getAllocation().getInfo().pMappedData;
	if (mapped == nullptr) {
		TOAST_CRITICAL("RawTextureUpload", "Raw texture staging buffer is not mapped");
	}
	std::memcpy(mapped, m_data.data(), m_data.size());
}

void RawTextureUpload::record(vk::CommandBuffer cmd) {
	const vk::Image image = m_texture->getImage();

	vk::ImageMemoryBarrier barrier {};
	barrier.oldLayout = vk::ImageLayout::eUndefined;
	barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = {};
	barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

	vk::BufferImageCopy region {};
	region.bufferOffset = 0;
	region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = vk::Extent3D {m_width, m_height, 1};

	cmd.copyBufferToImage(*m_staging_buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;

	barrier.dstAccessMask = {};

	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, nullptr, barrier
	);
}

}
