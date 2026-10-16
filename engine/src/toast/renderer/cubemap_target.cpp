/**
 * @file cubemap_target.cpp
 * @author dario
 * @date 14/08/2026
 */

#include "cubemap_target.hpp"

#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <format>
#include <string>
#include <tracy/Tracy.hpp>

namespace renderer {

void CubemapTarget::create(
    const VulkanCore& core, vk::Format format, uint32_t size, uint32_t mip_levels, std::string_view debug_name,
    vk::ImageUsageFlags extra_usage, bool with_face_views
) {
	ZoneScoped;
	const auto& device = core.getDevice();

	m_face_views.clear();
	m_cube_view.reset();
	m_image.reset();

	m_size = size;
	m_mip_levels = std::max(mip_levels, 1u);
	m_state = {};

	vk::ImageCreateInfo image_ci {};
	// imageCubeArray is not enabled on this device
	image_ci.flags = vk::ImageCreateFlagBits::eCubeCompatible;
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = format;
	image_ci.extent = vk::Extent3D {size, size, 1};
	image_ci.mipLevels = m_mip_levels;
	image_ci.arrayLayers = k_faces;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | extra_usage;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_image.emplace(core.getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(core, **m_image, std::string(debug_name));
	m_state.reset(**m_image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, m_mip_levels, 0, k_faces));

	vk::ImageViewCreateInfo cube_view_ci {};
	cube_view_ci.image = **m_image;
	cube_view_ci.viewType = vk::ImageViewType::eCube;
	cube_view_ci.format = format;
	cube_view_ci.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, m_mip_levels, 0, k_faces);
	m_cube_view.emplace(device, cube_view_ci);
	setDebugName(core, **m_cube_view, std::format("{} CubeView", debug_name));

	if (!with_face_views) {
		return;
	}

	m_face_views.reserve(static_cast<size_t>(m_mip_levels) * k_faces);
	for (uint32_t mip = 0; mip < m_mip_levels; ++mip) {
		for (uint32_t face = 0; face < k_faces; ++face) {
			vk::ImageViewCreateInfo face_view_ci {};
			face_view_ci.image = **m_image;
			face_view_ci.viewType = vk::ImageViewType::e2D;
			face_view_ci.format = format;
			face_view_ci.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, mip, 1, face, 1);
			m_face_views.emplace_back(device, face_view_ci);
			setDebugName(core, *m_face_views.back(), std::format("{} Face[{},{}]", debug_name, mip, face));
		}
	}
}

void CubemapTarget::transition(vk::CommandBuffer cmd, sync::Usage next) {
	sync::transition(cmd, m_state, next);
}

auto CubemapTarget::faceView(uint32_t mip, uint32_t face) const -> vk::ImageView {
	const size_t index = (static_cast<size_t>(mip) * k_faces) + face;
	if (index >= m_face_views.size()) {
		return {};
	}
	return *m_face_views[index];
}

}
