/// @file vulkan_texture.cpp
/// @author dario
/// @date 6/28/2026.

#include "vulkan_texture.hpp"

#include "toast/log.hpp"

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

	// 2D or 3D images
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

	// Match view type to image geom
	if (m_params.extent.depth > 1) {
		viewCI.viewType = vk::ImageViewType::e3D;
	} else {
		viewCI.viewType = m_params.layerCount > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
	}
	// if (isCubemap) viewCI.viewType = vk::ImageViewType::eCube;

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

	// Handle transcoders
	if (ktxTexture2_NeedsTranscoding(m_ktxTexture)) {
		// Transcode into an optimal native hardware format
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

	// Vulkan images require at least 1 mip level and 1 layer
	m_texParams.mipLevels = std::max(1u, m_ktxTexture->numLevels);
	m_texParams.layerCount = std::max(1u, m_ktxTexture->numLayers);

	// If it's a Cubemap, Vulkan treats cubemaps as a 6-layer 2D image
	if (m_ktxTexture->isCubemap) {
		m_texParams.layerCount = 6 * std::max(1u, m_ktxTexture->numLayers);
	}

	m_texture->create(core, m_texParams);
	m_texture->markUploading();

	// Create single linear hosting staging buffer
	const vk::DeviceSize totalSize = m_ktxTexture->dataSize;

	vk::BufferCreateInfo stagingCI {};
	stagingCI.size = totalSize;
	stagingCI.usage = vk::BufferUsageFlagBits::eTransferSrc;

	vma::AllocationCreateInfo allocCI {};
	allocCI.usage = vma::MemoryUsage::eAutoPreferHost;
	allocCI.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_stagingBuffer = core.getAllocator().createBuffer(stagingCI, allocCI);

	uint8_t* mappedData = static_cast<uint8_t*>(m_stagingBuffer.getAllocation().getInfo().pMappedData);
	std::memcpy(mappedData, m_ktxTexture->pData, totalSize);
	m_stagingBuffer.getAllocation().flush(0, totalSize);

	// Vulkan treats cubemap faces just as contiguous array layersx
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

				region.imageSubresource.baseArrayLayer = (layer * numFaces) + face;

				region.imageSubresource.layerCount = 1;

				region.imageExtent.width = std::max(1u, m_texParams.extent.width >> mip);
				region.imageExtent.height = std::max(1u, m_texParams.extent.height >> mip);
				region.imageExtent.depth = std::max(1u, m_texParams.extent.depth >> mip);

				m_copyRegions.push_back(region);
			}
		}
	}
}

void TextureUpload::record(vk::CommandBuffer cmd) {
	vk::Image imageHandle = m_texture->getImage();

	// Transition Image Layout UNDEFINED -> TRANSFER_DST_OPTIMAL
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

	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

	cmd.copyBufferToImage(*m_stagingBuffer, imageHandle, vk::ImageLayout::eTransferDstOptimal, m_copyRegions);

	// Transition Image Layout TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier
	);
}

}
