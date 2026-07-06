/// @file vulkan_texture.cpp
/// @author dario
/// @date 6/28/2026.

#include "vulkan_texture.hpp"

#include "toast/log.hpp"

#include <cstring>

namespace toast::renderer {

void VulkanTexture::create(const VulkanCore& core, Params params) {
	m_params = params;

	vk::ImageCreateInfo imageCI {};
	imageCI.format = m_params.format;
	imageCI.extent = m_params.extent;
	imageCI.mipLevels = m_params.mipLevels;
	imageCI.arrayLayers = m_params.layerCount;
	imageCI.samples = vk::SampleCountFlagBits::e1;
	imageCI.tiling = vk::ImageTiling::eOptimal;
	imageCI.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	imageCI.sharingMode = vk::SharingMode::eExclusive;
	imageCI.initialLayout = vk::ImageLayout::eUndefined;

	if (m_params.isCubemap) {
		imageCI.flags |= vk::ImageCreateFlagBits::eCubeCompatible;
	}

	if (m_params.extent.depth > 1) {
		imageCI.imageType = vk::ImageType::e3D;
	} else {
		imageCI.imageType = vk::ImageType::e2D;
	}

	vma::AllocationCreateInfo allocCI {};
	allocCI.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_image.emplace(core.getAllocator().createImage(imageCI, allocCI));

	vk::ImageViewCreateInfo viewCI {};
	viewCI.image = **m_image;

	if (m_params.extent.depth > 1) {
		viewCI.viewType = vk::ImageViewType::e3D;
	} else if (m_params.isCubemap) {
		// A  cubemap has 6 layers
		viewCI.viewType = m_params.layerCount > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
	} else {
		viewCI.viewType = m_params.layerCount > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
	}

	viewCI.format = m_params.format;
	viewCI.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	viewCI.subresourceRange.baseMipLevel = 0;
	viewCI.subresourceRange.levelCount = m_params.mipLevels;
	viewCI.subresourceRange.baseArrayLayer = 0;
	viewCI.subresourceRange.layerCount = m_params.layerCount;

	m_imageView = vk::raii::ImageView(core.getDevice(), viewCI);
}

void VulkanTexture::destroy() {
	m_imageView.clear();
	m_image.reset();
}

// Upload Functions

void TextureUpload::build(const VulkanCore& core) {
	auto result = ktxTexture2_CreateFromMemory(m_data.data(), m_data.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &m_ktxTexture);
	if (result != KTX_SUCCESS) {
		TOAST_CRITICAL("TextureUpload", "Failed to open KTX image data?!");
	}

	if (ktxTexture2_NeedsTranscoding(m_ktxTexture)) {
		result = ktxTexture2_TranscodeBasis(reinterpret_cast<ktxTexture2*>(m_ktxTexture), KTX_TTF_BC7_RGBA, 0);
		if (result != KTX_SUCCESS) {
			TOAST_CRITICAL("TextureUpload", "Failed to transcode BasisUniversal texture");
		}
	}

	m_texParams.format = static_cast<vk::Format>(m_ktxTexture->vkFormat);

	if (m_texParams.format == vk::Format::eR8G8B8Unorm || m_texParams.format == vk::Format::eR8G8B8Srgb) {
		TOAST_CRITICAL("TextureUpload", "24bit format is not supported by ToastEngine, Please use RGBA format!!");
	}

	m_texParams.extent = vk::Extent3D(m_ktxTexture->baseWidth, m_ktxTexture->baseHeight, m_ktxTexture->baseDepth);
	m_texParams.mipLevels = std::max(1u, m_ktxTexture->numLevels);
	m_texParams.layerCount = std::max(1u, m_ktxTexture->numLayers);

	// TODO: PROPER CUBEMAP SUPPORT
	m_texParams.isCubemap = m_ktxTexture->isCubemap;

	if (m_ktxTexture->isCubemap) {
		m_texParams.layerCount = 6 * std::max(1u, m_ktxTexture->numLayers);
	}

	m_texture->create(core, m_texParams);
	m_texture->markUploading();

	const vk::DeviceSize totalSize = m_ktxTexture->dataSize;

	vk::BufferCreateInfo stagingCI {};
	stagingCI.size = totalSize;
	stagingCI.usage = vk::BufferUsageFlagBits::eTransferSrc;

	vma::AllocationCreateInfo allocCI {};
	allocCI.usage = vma::MemoryUsage::eAuto;
	allocCI.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_stagingBuffer = core.getAllocator().createBuffer(stagingCI, allocCI);

	uint8_t* mappedData = static_cast<uint8_t*>(m_stagingBuffer.getAllocation().getInfo().pMappedData);
	std::memcpy(mappedData, m_ktxTexture->pData, totalSize);

	uint32_t numLayers = std::max(1u, m_ktxTexture->numLayers);
	uint32_t numFaces = m_ktxTexture->isCubemap ? 6 : 1;

	for (uint32_t mip = 0; mip < m_texParams.mipLevels; ++mip) {
		for (uint32_t layer = 0; layer < numLayers; ++layer) {
			for (uint32_t face = 0; face < numFaces; ++face) {
				ktx_size_t offset = 0;

				if (ktxTexture2_GetImageOffset(m_ktxTexture, mip, layer, face, &offset) != KTX_SUCCESS) {
					continue;
				}

				vk::BufferImageCopy region {};
				region.bufferOffset = offset;
				region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				region.imageSubresource.mipLevel = mip;
				// Maps faces directly into contiguous array layers
				region.imageSubresource.baseArrayLayer = (layer * numFaces) + face;
				region.imageSubresource.layerCount = 1;

				region.imageExtent.width = std::max(1u, m_texParams.extent.width / (1 << mip));
				region.imageExtent.height = std::max(1u, m_texParams.extent.height / (1 << mip));
				region.imageExtent.depth = std::max(1u, m_texParams.extent.depth / (1 << mip));

				m_copyRegions.push_back(region);
			}
		}
	}
}

void TextureUpload::record(vk::CommandBuffer cmd) {
	vk::Image imageHandle = m_texture->getImage();

	vk::ImageMemoryBarrier barrier {};
	barrier.oldLayout = vk::ImageLayout::eUndefined;
	barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = imageHandle;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = m_texParams.mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = m_texParams.layerCount;
	barrier.srcAccessMask = {};
	barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

	cmd.copyBufferToImage(*m_stagingBuffer, imageHandle, vk::ImageLayout::eTransferDstOptimal, m_copyRegions);

	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	// PERF: If your textures are read by more than just the fragment shader
	// consider switching eFragmentShader to eAllGraphics
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier
	);
}

void RawTextureUpload::build(const VulkanCore& core) {
	if (m_width == 0 || m_height == 0 || m_data.empty()) {
		TOAST_CRITICAL("RawTextureUpload", "Cannot upload raw texture with empty dimensions/data");
	}

	VulkanTexture::Params params {};
	params.format = m_format;
	params.extent = vk::Extent3D(m_width, m_height, 1);
	params.mipLevels = 1;
	params.layerCount = 1;
	params.isCubemap = false;
	m_texture->create(core, params);
	m_texture->markUploading();

	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = static_cast<vk::DeviceSize>(m_data.size());
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
	m_staging_buffer = core.getAllocator().createBuffer(staging_ci, alloc_ci);

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
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier
	);
}

}
