#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc_raii.hpp>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct FrameResources {
	std::optional<vma::raii::Buffer> staging_buffer;
	std::optional<vma::raii::Buffer> gpu_buffer;
	vk::raii::DescriptorSet descriptor_set = nullptr;
};

namespace renderer {

struct ShGridKey {
	glm::vec3 min_corner {0.0f};
	glm::vec3 extents {0.0f};
	glm::uvec3 counts {0};
};

/// xyz world normal w roughness
inline constexpr vk::Format k_scene_normal_format = vk::Format::eR16G16B16A16Sfloat;

inline constexpr vk::Format k_scene_indirect_format = vk::Format::eB10G11R11UfloatPack32;

/// @warning RenderStage::world only since the overlay scope has one attachment
[[nodiscard]]
inline auto worldStageExtraColorFormats() -> std::vector<vk::Format> {
	return {k_scene_normal_format, k_scene_indirect_format};
}

[[nodiscard]]
inline auto colorSubresourceRange() -> vk::ImageSubresourceRange {
	return {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
}

inline void submitAndWait(const vk::raii::Device& device, vk::Queue queue, vk::CommandBuffer cmd) {
	const vk::raii::Fence fence(device, vk::FenceCreateInfo {});

	vk::SubmitInfo submit {};
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	queue.submit(submit, *fence);

	std::ignore = device.waitForFences(*fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
}

inline void recordUndefinedToTransferDst(vk::CommandBuffer cmd, vk::Image image, const vk::ImageSubresourceRange& range) {
	vk::ImageMemoryBarrier to_dst {};
	to_dst.oldLayout = vk::ImageLayout::eUndefined;
	to_dst.newLayout = vk::ImageLayout::eTransferDstOptimal;
	to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_dst.image = image;
	to_dst.subresourceRange = range;
	to_dst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, to_dst);
}

inline void recordTransferDstToShaderRead(vk::CommandBuffer cmd, vk::Image image, const vk::ImageSubresourceRange& range) {
	vk::ImageMemoryBarrier to_read {};
	to_read.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	to_read.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_read.image = image;
	to_read.subresourceRange = range;
	to_read.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	to_read.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, to_read
	);
}

[[nodiscard]]
inline auto linearClampSamplerInfo() -> vk::SamplerCreateInfo {
	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = vk::Filter::eLinear;
	sampler_ci.minFilter = vk::Filter::eLinear;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eNearest;
	return sampler_ci;
}

[[nodiscard]]
inline auto nearestClampSamplerInfo() -> vk::SamplerCreateInfo {
	auto sampler_ci = linearClampSamplerInfo();
	sampler_ci.magFilter = vk::Filter::eNearest;
	sampler_ci.minFilter = vk::Filter::eNearest;
	return sampler_ci;
}

[[nodiscard]]
inline auto linearClampMippedSamplerInfo(float max_lod) -> vk::SamplerCreateInfo {
	auto sampler_ci = linearClampSamplerInfo();
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eLinear;
	sampler_ci.maxLod = max_lod;
	return sampler_ci;
}

[[nodiscard]]
inline auto colorTargetImageInfo(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage = {}) -> vk::ImageCreateInfo {
	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = format;
	image_ci.extent = vk::Extent3D {extent.width, extent.height, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 1;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | usage;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;
	return image_ci;
}

}
