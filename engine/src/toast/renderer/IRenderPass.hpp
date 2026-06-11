/// @file IRenderPass.hpp
/// @author dario
/// @date 07/06/2026.

#pragma once

#include "vulkan_common.hpp"

class IRenderPass {
public:
	virtual ~IRenderPass() = default;

	virtual void update(uint32_t frame_index, float dt) { }

	virtual void record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) = 0;

protected:
	std::vector<FrameResources> m_frameResources;
};
