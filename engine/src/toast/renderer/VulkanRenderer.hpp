/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "IOutputTarget.hpp"
#include "VulkanCore.hpp"
#include "VulkanPipeline.hpp"

#include <memory>
#include <vector>

namespace toast::renderer {

class VulkanRenderer {
public:
	struct FrameContext {
		vk::raii::CommandBuffer commandBuffer = nullptr;
		vk::raii::Semaphore imageAvailable = nullptr;
		vk::raii::Fence inFlight = nullptr;
	};

	VulkanRenderer(
	    const VulkanCore& core, std::unique_ptr<IOutputTarget> outputTarget, std::unique_ptr<VulkanPipeline> pipeline,
	    uint32_t framesInFlight = 10
	);
	~VulkanRenderer() = default;

	VulkanRenderer(const VulkanRenderer&) = delete;
	VulkanRenderer& operator=(const VulkanRenderer&) = delete;
	VulkanRenderer(VulkanRenderer&&) = delete;
	VulkanRenderer& operator=(VulkanRenderer&&) = delete;

	void drawFrame();
	void resize(vk::Extent2D extent);

	[[nodiscard]]
	const IOutputTarget& getOutputTarget() const {
		return *m_outputTarget;
	}

	[[nodiscard]]
	IOutputTarget& getOutputTarget() {
		return *m_outputTarget;
	}

private:
	void createCommandPool();
	void createFrameContexts(uint32_t framesInFlight);
	void createPerImageSync();
	void recordFrame(FrameContext& frame, uint32_t imageIndex);

	const VulkanCore* m_core = nullptr;
	std::unique_ptr<IOutputTarget> m_outputTarget;
	std::unique_ptr<VulkanPipeline> m_pipeline;

	vk::raii::CommandPool m_commandPool = nullptr;
	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_renderFinishedPerImage;
	std::vector<vk::Fence> m_imagesInFlight;
	std::vector<vk::ImageLayout> m_imageLayouts;
	uint32_t m_currentFrame = 0;
};

}
