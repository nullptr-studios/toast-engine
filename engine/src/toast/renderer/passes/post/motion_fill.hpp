/**
 * @file motion_fill.hpp
 * @author dario
 * @date 22/11/2026
 */

#pragma once

#include "toast/renderer/core/descriptor_cache.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

/// Gives every render texel a motion vector by rebuilding the unwritten ones from depth reprojection
/// @note Render thread only
class MotionFill {
public:
	MotionFill(const VulkanCore& core, vk::Extent2D render_extent);

	void onResize(vk::Extent2D render_extent);

	/// False when not ready and nothing was recorded
	auto record(vk::CommandBuffer cmd, uint32_t frame_index, const glm::mat4& reprojection, bool ignore_written) -> bool;

	/// RGBA16F xy current minus previous uv, z reactive, w 1 when the scene wrote it
	[[nodiscard]]
	auto motionView() const -> vk::ImageView {
		return m_motion.view.has_value() ? **m_motion.view : vk::ImageView {};
	}

	/// R8 reactive in [0,1]
	[[nodiscard]]
	auto reactiveView() const -> vk::ImageView {
		return m_reactive.view.has_value() ? **m_reactive.view : vk::ImageView {};
	}

	[[nodiscard]]
	auto motionImage() const -> vk::Image {
		return m_motion.state.image;
	}

	[[nodiscard]]
	auto reactiveImage() const -> vk::Image {
		return m_reactive.state.image;
	}

private:
	/// Mirrors motion_fill.slang MotionFillParams
	struct Push {
		glm::mat4 reprojection {1.0f};
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t ignore_written = 0;
	};

	struct Target {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		mutable sync::ImageState state;
	};

	void createTarget(Target& target, vk::Format format, std::string_view debug_name) const;

	const VulkanCore* m_core = nullptr;
	vk::Extent2D m_extent {};

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;
	vk::raii::Sampler m_point_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	/// Depth, scene motion, filled motion and reactive last written into each set
	DescriptorCache<std::array<vk::ImageView, 4>> m_bound_views;

	Target m_motion;
	Target m_reactive;
};

}
