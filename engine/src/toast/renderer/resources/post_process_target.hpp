/**
 * @file post_process_target.hpp
 * @author dario
 * @date 14/08/2026
 */

#pragma once

#include "toast/renderer/core/vulkan_common.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace renderer {
class VulkanCore;

/// @note Render thread only
class PostProcessTarget {
public:
	/// Rounded up so odd sizes cover the last row. Here because file local copies collide in the unity build
	[[nodiscard]]
	static auto halfExtent(vk::Extent2D extent) noexcept -> vk::Extent2D {
		const uint32_t width = (extent.width + 1) / 2;
		const uint32_t height = (extent.height + 1) / 2;
		return {width > 0 ? width : 1u, height > 0 ? height : 1u};
	}

	/// Image behind a live target view or null. Post passes only hand on views and NGX also needs the image
	[[nodiscard]]
	static auto imageOf(vk::ImageView view) -> vk::Image;

	PostProcessTarget() = default;
	~PostProcessTarget();
	PostProcessTarget(const PostProcessTarget&) = delete;
	auto operator=(const PostProcessTarget&) -> PostProcessTarget& = delete;

	/// @p usage adds to colour attachment and sampled
	void create(
	    const VulkanCore& core, vk::Extent2D extent, vk::Format format, std::string_view debug_name, vk::ImageUsageFlags usage = {}
	);

	void beginScope(vk::CommandBuffer cmd) const;

	void endScope(vk::CommandBuffer cmd) const;

	/// Makes a never drawn target sampleable for descriptors
	void makeSampleable(vk::CommandBuffer cmd) const;

	[[nodiscard]]
	auto view() const -> vk::ImageView {
		return m_view.has_value() ? **m_view : vk::ImageView {};
	}

	/// For barriers recorded outside the scopes, such as a compute write
	[[nodiscard]]
	auto state() const noexcept -> sync::ImageState& {
		return m_state;
	}

	[[nodiscard]]
	auto extent() const noexcept -> vk::Extent2D {
		return m_extent;
	}

	[[nodiscard]]
	auto isReady() const noexcept -> bool {
		return m_image.has_value() && m_view.has_value();
	}

private:
	std::optional<vma::raii::Image> m_image;
	std::optional<vk::raii::ImageView> m_view;
	vk::Extent2D m_extent {};

	mutable sync::ImageState m_state;
};

}
