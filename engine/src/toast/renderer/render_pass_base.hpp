/// @file IRenderPass.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include "vulkan_common.hpp"

#include <atomic>
#include <string_view>

/**
 * @brief Interface for custom rendering passes
 */
class IRenderPass {
public:
	virtual ~IRenderPass() = default;

	/**
	 * @brief Updates the render pass state for the current frame
	 * @param frame_index The index of the current frame in flight
	 * @param dt The delta time since the last frame
	 */
	virtual void update(uint32_t frame_index, float dt) { }

	/**
	 * @brief Records work that must run before the renderer's main scope opens
	 * @param cmd The command buffer to record commands into
	 * @param frame_index The index of the current frame in flight
	 * @param image_index The index of the image to render to
	 *
	 * Runs outside beginRendering/endRendering, so passes can render to their own targets
	 * and transition them for sampling
	 */
	virtual void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) { }

	/**
	 * @brief Records the render pass commands for the current frame
	 * @param cmd The command buffer to record commands into
	 * @param frameIndex The index of the current frame in flight
	 * @param imageIndex The index of the image to render to
	 */
	virtual void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) = 0;

	/// @returns name shown in the editor's pass visibility popup
	[[nodiscard]]
	virtual auto name() const -> std::string_view {
		return "Pass";
	}

	/// Disabled passes are skipped by the renderer's record loop
	void setEnabled(bool enabled) noexcept { m_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto isEnabled() const noexcept -> bool {
		return m_enabled.load(std::memory_order_relaxed);
	}

protected:
	std::vector<FrameResources> m_frame_resources;

private:
	std::atomic_bool m_enabled {true};
};
