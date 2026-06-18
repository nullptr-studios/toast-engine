/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "../Camera.hpp"
#include "IOutputTarget.hpp"
#include "IRenderPass.hpp"
#include "VulkanCore.hpp"
#include "VulkanMesh.hpp"
#include "VulkanPipeline.hpp"

#include <array>
#include <memory>
#include <optional>
#include <semaphore>
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

	static constexpr uint32_t kFramesInFlight = 3;

	static constexpr uint8_t kRenderFrames = 3;    // do not confuse with konflightframes

	struct PendingMeshUpload {
		VulkanMesh* mesh = nullptr;
		vma::raii::Buffer vertexStaging = nullptr;
		vma::raii::Buffer indexStaging = nullptr;
		vk::raii::Fence completionFence = nullptr;
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

	struct FrameUBO {
		glm::mat4 view;
		glm::mat4 projection;
		glm::mat4 viewProjection;

		glm::vec3 cameraPosition;

		float time;
	};

	struct DrawCommand {
		VulkanMesh* mesh;

		glm::mat4 transform;
	};

	struct RenderFrame {
		FrameUBO frameData;

		std::vector<DrawCommand> draws;
	};

	VulkanRenderer(const VulkanCore& core, std::unique_ptr<IOutputTarget> outputTarget);

	~VulkanRenderer();

	VulkanRenderer(const VulkanRenderer&) = delete;
	VulkanRenderer& operator=(const VulkanRenderer&) = delete;
	VulkanRenderer(VulkanRenderer&&) = delete;
	VulkanRenderer& operator=(VulkanRenderer&&) = delete;

	void start();

	auto beginFrameBuild() -> RenderFrame& { return m_render_frames[m_writeIndex]; }

	std::counting_semaphore<kRenderFrames>& getFreeFramesSemaphore() { return m_freeFrames; }

	void submitFrame();

	void stop();

	void resize(vk::Extent2D extent);

	void addRenderPass(std::unique_ptr<IRenderPass> pass);

	void queueMeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data);

	auto getFrameUBORes(uint32_t currentFrame) const -> const FrameResources* { return &m_frameUBORes[currentFrame]; }

	void setActiveCamera(Camera* camera);

	Camera* getActiveCamera() { return m_camera; }

	[[nodiscard]]
	const IOutputTarget& getOutputTarget() const {
		return *m_outputTarget;
	}

	static VulkanRenderer* instance;

private:
	void drawFrame(RenderFrame& frameData);

	void mainRenderThread();

	bool m_running = false;

	std::thread m_render_thread;

	std::array<RenderFrame, kRenderFrames> m_render_frames;
	std::atomic<uint32_t> m_writeIndex = 0;
	std::atomic<uint32_t> m_readIndex = 0;

	std::mutex m_queueMutex;

	std::condition_variable m_frameCV;

	std::queue<uint32_t> m_readyFrames;
	RenderFrame m_cachedFrame;
	bool m_hasCachedFrame = false;

	std::counting_semaphore<kRenderFrames> m_freeFrames {kRenderFrames};

	struct DepthResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
	};

	void createGraphicsCommandPool();
	void createTransferCommandPool();
	void createFrameContexts();
	void createPerImageSync();
	void createDepthResources();
	void createDescriptorPool();

	void recordTransferPass(FrameContext& frame);

	void recordComputePass(FrameContext& frame, uint32_t imageIndex);

	void recordFrame(FrameContext& frame, uint32_t imageIndex);

	void processPendingUploads();

	const VulkanCore* m_core = nullptr;

	std::unique_ptr<IOutputTarget> m_outputTarget;
	std::vector<std::unique_ptr<IRenderPass>> m_renderPasses;
	vk::Format m_depthFormat = vk::Format::eUndefined;
	DepthResources m_depthResources;
	vk::ImageLayout m_depthLayout = vk::ImageLayout::eUndefined;

	std::vector<PendingMeshUpload> m_pendingUploads;

	vk::raii::CommandPool m_commandPool = nullptr;

	vk::raii::CommandPool m_transferCommandPool = nullptr;

	vk::raii::DescriptorPool m_descriptorPool = nullptr;

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_renderFinishedPerImage;
	std::vector<vk::Fence> m_imagesInFlight;
	uint32_t m_currentFrame = 0;

	// MAYBE EXPAND THIS??
	Camera* m_camera = nullptr;

	// FrameUBO
	std::vector<FrameUBO> m_frameUBOs;
	std::vector<FrameResources> m_frameUBORes;
	vk::raii::PipelineLayout m_frameUBOPipelineLayout = nullptr;

	void createFrameResources();
	void updateFrameResources(uint32_t frameIndex, RenderFrame& frameData);
};

static void start() {
	VulkanRenderer::instance->start();
}

static void stop() {
	VulkanRenderer::instance->stop();
}

static auto beginFrameBuild() -> VulkanRenderer::RenderFrame& {
	return VulkanRenderer::instance->beginFrameBuild();
}

static void submitFrame() {
	VulkanRenderer::instance->submitFrame();
}

static Camera* getActiveCamera() {
	return VulkanRenderer::instance->getActiveCamera();
}

static void setActiveCamera(Camera* camera) {
	VulkanRenderer::instance->setActiveCamera(camera);
}

}
