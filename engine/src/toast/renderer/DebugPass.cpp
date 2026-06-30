/// @file DebugPass.cpp
/// @author dario
/// @date 10/06/2026.

#include "DebugPass.hpp"

toast::renderer::DebugPass::DebugPass(
    const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent
) { }

void toast::renderer::DebugPass::record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) { }

void toast::renderer::DebugPass::update(uint32_t frame_index, float dt) { }

void toast::renderer::DebugPass::CreateResources(const toast::renderer::VulkanCore& core) { }
