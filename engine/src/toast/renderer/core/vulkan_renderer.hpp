/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "../Camera.hpp"
#include "output_target_base.hpp"
#include "render_pass_base.hpp"
#include "toast/events/event.inl"
#include "toast/events/listener.hpp"
#include "vulkan_core.hpp"
#include "vulkan_mesh.hpp"
#include "vulkan_pipeline.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <semaphore>
#include <thread>
#include <vector>

namespace toast::renderer {

/**
 * @brief Coordinates rendering by managing frame submissions, render passes, and GPU synchronization
 *
 * Runs on a separate render thread and handles frame timing, depth resources,
 * and mesh upload queues.
 */
class VulkanRenderer {
public:
	static auto selectDepthFormat(const VulkanCore& core) -> vk::Format;

	static constexpr uint32_t kFramesInFlight = 3;

	static constexpr uint8_t kRenderFrames = 3;    // Number of frames queued for rendering (separate from kFramesInFlight)

	struct PendingMeshUpload {
		VulkanMesh* mesh = nullptr;
		vma::raii::Buffer vertex_staging = nullptr;
		vma::raii::Buffer index_staging = nullptr;
		vk::raii::Fence completion_fence = nullptr;
	};

	struct FrameContext {
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::CommandBuffer transfer_command_buffer = nullptr;
		vk::raii::Semaphore image_available = nullptr;
		vk::raii::Semaphore transfer_finished = nullptr;
		vk::raii::Fence in_flight = nullptr;
		uint32_t last_image_index = 0;
		bool has_submitted = false;
	};

	struct FrameUBO {
		glm::mat4 view;
		glm::mat4 projection;
		glm::mat4 view_projection;

		glm::vec3 camera_position;

		float time;
	};

	struct DrawCommand {
		VulkanMesh* mesh;

		glm::mat4 transform;
	};

	struct RenderFrame {
		FrameUBO frame_data;

		std::vector<DrawCommand> draws;
	};

	VulkanRenderer(const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target);

	~VulkanRenderer();

	VulkanRenderer(const VulkanRenderer&) = delete;
	auto operator=(const VulkanRenderer&) -> VulkanRenderer& = delete;
	VulkanRenderer(VulkanRenderer&&) = delete;
	auto operator=(VulkanRenderer&&) -> VulkanRenderer& = delete;

	void start();

	auto beginFrameBuild() -> RenderFrame& { return m_render_frames[m_write_index]; }

	std::counting_semaphore<kRenderFrames>& getFreeFramesSemaphore() { return m_free_frames; }

	void submitFrame();

	void stop();

	void resize(vk::Extent2D extent);

	void addRenderPass(std::unique_ptr<IRenderPass> pass);

	void queueMeshUpload(VulkanMesh& mesh, VulkanMesh::UploadData data);

	auto getFrameUBORes(uint32_t current_frame) const -> const FrameResources* { return &m_frame_ubo_res[current_frame]; }

	void setActiveCamera(Camera* camera);

	Camera* getActiveCamera() { return m_camera; }

	[[nodiscard]]
	const IOutputTarget& getOutputTarget() const {
		return *m_output_target;
	}

	static VulkanRenderer* instance;

private:
	void drawFrame(RenderFrame& frame_data);

	void mainRenderThread();

	event::Listener m_listener;

	std::atomic_bool m_running {false};

	std::thread m_render_thread;

	std::array<RenderFrame, kRenderFrames> m_render_frames;
	std::atomic<uint32_t> m_write_index = 0;
	std::atomic<uint32_t> m_read_index = 0;

	std::mutex m_queue_mutex;

	std::condition_variable m_frame_cv;

	std::queue<uint32_t> m_ready_frames;
	RenderFrame m_cached_frame;
	bool m_has_cached_frame = false;

	std::counting_semaphore<kRenderFrames> m_free_frames {kRenderFrames};

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

	void recordComputePass(FrameContext& frame, uint32_t image_index);

	void recordFrame(FrameContext& frame, uint32_t image_index);

	void processPendingUploads();

	const VulkanCore* m_core = nullptr;

	std::unique_ptr<IOutputTarget> m_output_target;
	std::vector<std::unique_ptr<IRenderPass>> m_render_passes;
	vk::Format m_depth_format = vk::Format::eUndefined;
	DepthResources m_depth_resources;
	vk::ImageLayout m_depth_layout = vk::ImageLayout::eUndefined;

	std::vector<PendingMeshUpload> m_pending_uploads;

	vk::raii::CommandPool m_command_pool = nullptr;

	vk::raii::CommandPool m_transfer_command_pool = nullptr;

	vk::raii::DescriptorPool m_descriptor_pool = nullptr;

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_render_finished_per_image;
	std::vector<vk::Fence> m_images_in_flight;
	uint32_t m_current_frame = 0;

	/// Active camera for the renderer. Can be nullptr if no camera is set.
	Camera* m_camera = nullptr;

	// FrameUBO and related resources
	std::vector<FrameUBO> m_frame_ubos;
	std::vector<FrameResources> m_frame_ubo_res;
	vk::raii::PipelineLayout m_frame_ubo_pipeline_layout = nullptr;

	void createFrameResources();
	void updateFrameResources(uint32_t frame_index, RenderFrame& frame_data);
};

// Free functions for convenient access to the singleton renderer
inline void start() {
	VulkanRenderer::instance->start();
}

inline void stop() {
	VulkanRenderer::instance->stop();
}

inline auto beginFrameBuild() -> VulkanRenderer::RenderFrame& {
	return VulkanRenderer::instance->beginFrameBuild();
}

inline void submitFrame() {
	VulkanRenderer::instance->submitFrame();
}

inline Camera* getActiveCamera() {
	return VulkanRenderer::instance->getActiveCamera();
}

inline void setActiveCamera(Camera* camera) {
	VulkanRenderer::instance->setActiveCamera(camera);
}

}    // namespace toast::renderer
