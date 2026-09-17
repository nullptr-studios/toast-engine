/**
 * @file cubemap_target.hpp
 * @author dario
 * @date 14/08/2026
 */

#pragma once

#include "vulkan_common.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace renderer {
class VulkanCore;

class CubemapTarget {
public:
	void create(
	    const VulkanCore& core, vk::Format format, uint32_t size, uint32_t mip_levels, std::string_view debug_name,
	    vk::ImageUsageFlags extra_usage = {}, bool with_face_views = true
	);

	void
	    transition(vk::CommandBuffer cmd, vk::ImageLayout new_layout, vk::AccessFlags dst_access, vk::PipelineStageFlags dst_stage);

	void setLayout(vk::ImageLayout layout) noexcept;

	[[nodiscard]]
	auto faceView(uint32_t mip, uint32_t face) const -> vk::ImageView;

	[[nodiscard]]
	auto cubeView() const -> vk::ImageView {
		return m_cube_view.has_value() ? **m_cube_view : vk::ImageView {};
	}

	[[nodiscard]]
	auto image() const -> vk::Image {
		return m_image.has_value() ? **m_image : vk::Image {};
	}

	[[nodiscard]]
	auto size() const noexcept -> uint32_t {
		return m_size;
	}

	[[nodiscard]]
	auto mipSize(uint32_t mip) const noexcept -> uint32_t {
		return std::max(m_size >> mip, 1u);
	}

	[[nodiscard]]
	auto mipLevels() const noexcept -> uint32_t {
		return m_mip_levels;
	}

	[[nodiscard]]
	auto layout() const noexcept -> vk::ImageLayout {
		return m_layout;
	}

	[[nodiscard]]
	auto isReady() const noexcept -> bool {
		return m_image.has_value() && m_cube_view.has_value();
	}

	static constexpr uint32_t k_faces = 6;

private:
	std::optional<vma::raii::Image> m_image;
	std::optional<vk::raii::ImageView> m_cube_view;
	/// One per mip and face in mip major order
	std::vector<vk::raii::ImageView> m_face_views;

	uint32_t m_size = 0;
	uint32_t m_mip_levels = 1;
	vk::ImageLayout m_layout = vk::ImageLayout::eUndefined;
};

}
