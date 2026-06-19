/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "IOutputTarget.hpp"
#include "VulkanCore.hpp"
#include "VulkanPipeline.hpp"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace toast::renderer {

/**
 * @brief Manages rendering operations and graphics resources using the Vulkan API.
 *
 * The VulkanRenderer class encapsulates the initialization of the graphics
 * pipeline and handles the lifecycle of render resources such as
 * swap chains, framebuffers, and command buffers.
 *
 * @details
 *
 * This class coordinates the rendering process by abstracting the
 * underlying Vulkan object handles. It ensures proper synchronization
 * between rendering threads and frame presentation operations.
 */
class VulkanRenderer {
public:
	static auto selectDepthFormat(const VulkanCore& core) -> vk::Format;

	struct FrameUniformData {
		std::array<float, 2> iResolution;
		float iTime;
		float padding;
	};

	struct FrameContext {
		vk::raii::CommandBuffer commandBuffer = nullptr;
		vk::raii::CommandBuffer transferCommandBuffer = nullptr;
		vk::raii::Semaphore imageAvailable = nullptr;
		vk::raii::Semaphore transferFinished = nullptr;
		vk::raii::Fence inFlight = nullptr;
		uint32_t lastImageIndex = 0;
		bool hasSubmitted = false;
	};

	VulkanRenderer(
	    const VulkanCore& core, std::unique_ptr<IOutputTarget> outputTarget, std::unique_ptr<VulkanPipeline> pipeline,
	    uint32_t framesInFlight = 3
	);

	~VulkanRenderer();

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
	struct DepthResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
	};

	struct FrameUniformResources {
		std::optional<vma::raii::Buffer> stagingBuffer;
		std::optional<vma::raii::Buffer> gpuBuffer;
		vk::DescriptorSet descriptorSet = nullptr;
	};

	void createGraphicsCommandPool();
	void createTransferCommandPool();
	void createFrameContexts(uint32_t framesInFlight);
	void createPerImageSync();
	void createDepthResources();

	void createFrameUniformResources();
	void updateFrameUniformData();

	void recordTransferPass(FrameContext& frame);

	void recordComputePass(FrameContext& frame, uint32_t imageIndex);

	void recordFrame(FrameContext& frame, uint32_t imageIndex);

	const VulkanCore* m_core = nullptr;

	std::unique_ptr<IOutputTarget> m_outputTarget;
	std::unique_ptr<VulkanPipeline> m_pipeline;
	vk::Format m_depthFormat = vk::Format::eUndefined;
	DepthResources m_depthResources;
	FrameUniformData m_frameUniformData {};    // DEBUGGING
	FrameUniformResources m_frameUniformResources;
	vk::ImageLayout m_depthLayout = vk::ImageLayout::eUndefined;

	vk::raii::CommandPool m_commandPool = nullptr;

	vk::raii::CommandPool m_transferCommandPool = nullptr;

	vk::raii::DescriptorPool m_descriptorPool = nullptr;

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_renderFinishedPerImage;
	std::vector<vk::Fence> m_imagesInFlight;
	uint32_t m_currentFrame = 0;

	// DEBUGGING
	float m_totalTime = 0.0f;
};

}
