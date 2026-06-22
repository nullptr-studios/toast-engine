/// @file IRenderPass.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include "vulkan_common.hpp"

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
	 * @brief Records the render pass commands for the current frame
	 * @param cmd The command buffer to record commands into
	 * @param frameIndex The index of the current frame in flight
	 * @param imageIndex The index of the image to render to
	 */
	virtual void record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) = 0;

protected:
	std::vector<FrameResources> m_frameResources;
};
