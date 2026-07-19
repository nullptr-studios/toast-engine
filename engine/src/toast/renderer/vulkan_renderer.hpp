/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "output_target_base.hpp"
#include "render_pass_base.hpp"
#include "vulkan_core.hpp"
#include "vulkan_mesh.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_texture.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <glm/gtc/constants.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <toast/assets/core_types.hpp>
#include <toast/events/event.inl>
#include <toast/events/listener.hpp>
#include <tracy/TracyVulkan.hpp>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace assets {
class Material;
}

namespace toast {
class Camera;
class MeshNode;
}

namespace renderer {

class MaterialPass;

/**
 * @brief Coordinates rendering by managing frame submissions, render passes, and GPU synchronization
 *
 * Runs on a separate render thread and handles frame timing, depth resources,
 * and mesh upload queues
 */
class VulkanRenderer {
public:
	[[nodiscard]]
	static auto selectDepthFormat(const VulkanCore& core) -> vk::Format;

	static constexpr uint32_t k_frames_in_flight = 3;

	static constexpr uint8_t k_render_frames = 3;    // Number of frames queued for rendering

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

	struct MeshInstanceProxy {
		VulkanMesh* mesh = nullptr;
		assets::Material* material = nullptr;
		assets::Material* root_material = nullptr;
		glm::mat4 model = glm::mat4(1.0f);
	};

	/// @brief One world-space UI panel drawn as a texture quad by ui::WorldUIPass
	struct UIWorldPanelProxy {
		vk::ImageView view = nullptr;         ///< panel output image, transitioned for sampling
		glm::mat4 model = glm::mat4(1.0f);    ///< node world transform; scale gives the metric size
	};

	/// @brief One vertex of an immediate-mode debug line; two consecutive vertices make one line segment
	struct DebugVertex {
		glm::vec<3, float, glm::packed_highp> position;
		glm::vec<4, float, glm::packed_highp> color;
	};

	static_assert(std::is_standard_layout_v<DebugVertex>, "DebugVertex must be standard layout");

	struct RenderFrame {
		FrameUBO frame_data;

		std::vector<MeshInstanceProxy> mesh_instances;

		// Immediate-mode debug draw data queued via debugDrawLine()/debugDrawBox()/debugDrawSphere()/
		// debugDrawAxes() dnd consumed by DebugPass
		std::vector<DebugVertex> debug_line_vertices;    // consecutive pairs; each pair is one line segment
		std::vector<glm::mat4> debug_gizmo_instances;    // one axis-triad gizmo draw per entry

		// Secondary command buffers recorded by ui::UISystem on the main thread
		std::vector<vk::CommandBuffer> ui_command_buffers;
		std::vector<vk::ImageView> ui_output_views;
		std::vector<UIWorldPanelProxy> ui_world_panels;    // drawn by ui::WorldUIPass
		std::shared_ptr<const void> ui_slot_guard;
	};

	/// @brief Callback that fills UI data into the frame being built
	using UIFrameBuilder = std::function<void(RenderFrame&)>;

	void setUIFrameBuilder(UIFrameBuilder builder) { m_ui_frame_builder = std::move(builder); }

	VulkanRenderer(const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target) noexcept;

	~VulkanRenderer();

	VulkanRenderer(const VulkanRenderer&) = delete;
	auto operator=(const VulkanRenderer&) -> VulkanRenderer& = delete;
	VulkanRenderer(VulkanRenderer&&) = delete;
	auto operator=(VulkanRenderer&&) -> VulkanRenderer& = delete;

	void start() noexcept;

	[[nodiscard]]
	auto beginFrameBuild() noexcept -> RenderFrame& {
		return m_render_frames[m_write_index];
	}

	[[nodiscard]]
	auto getFreeFramesSemaphore() noexcept -> std::counting_semaphore<k_render_frames>& {
		return m_free_frames;
	}

	void submitFrame() noexcept;

	/**
	 * @brief Builds the next RenderFrame from the active camera and registered mesh proxies, then submits it
	 *
	 * Called once per simulation tick; does nothing if there's no free render slot available
	 *
	 * @param time Elapsed time in seconds, forwarded into the frame's FrameUBO
	 */
	void tick(float time) noexcept;

	/// @brief Registers @p node so its mesh is drawn each frame; no-op if already registered
	void registerMeshNodeProxy(toast::MeshNode* node);

	/// @brief Unregisters @p node so it stops being drawn
	void unregisterMeshNodeProxy(toast::MeshNode* node);

	/**
	 * @brief Caps how often the render thread draws & presents a frame
	 *
	 * Applies uniformly: a brand new frame submitted faster than the cap is still paced to it, and an
	 * unchanged cached frame is redrawn no faster than the cap either
	 *
	 * @param max_fps Target draw rate in Hz. Pass 0 or a negative value to run fully uncapped
	 */
	void setFrameRateLimit(double max_fps) noexcept { m_frame_rate_limit_hz.store(max_fps, std::memory_order_relaxed); }

	[[nodiscard]]
	auto frameRateLimit() const noexcept -> double {
		return m_frame_rate_limit_hz.load(std::memory_order_relaxed);
	}

	void stop();

	void addRenderPass(std::unique_ptr<IRenderPass> pass);

	struct PassInfo {
		std::string name;
		bool enabled = true;
	};

	[[nodiscard]]
	auto listPasses() -> std::vector<PassInfo>;

	/// Enables/disables every pass matching @p name
	void setPassEnabled(std::string_view name, bool enabled);

	/// 1x1 white fallback for material texture slots without a ready texture
	[[nodiscard]]
	auto getDefaultTextureView() const noexcept -> vk::ImageView {
		return m_default_texture.getView();
	}

	[[nodiscard]]
	auto getDefaultSampler() const noexcept -> vk::Sampler {
		return *m_default_sampler;
	}

	void applyResize(vk::Extent2D extent);

	void queueResourceUpload(std::unique_ptr<PendingResourceUpload> upload);

	[[nodiscard]]
	auto getFrameUBORes(uint32_t current_frame) const -> const FrameResources* {
		return &m_frame_ubo_res[current_frame];
	}

	// Expose raw descriptor pool handle so render passes can allocate their own descriptor sets
	[[nodiscard]]
	auto getDescriptorPoolHandle() const noexcept -> vk::DescriptorPool {
		return *m_descriptor_pool;
	}

	[[nodiscard]]
	void setActiveCamera(toast::Camera* camera);

	[[nodiscard]]
	auto getCore() -> const VulkanCore& {
		return *m_core;
	}

	[[nodiscard]]
	auto getActiveCamera() -> toast::Camera* {
		return m_camera;
	}

	[[nodiscard]]
	auto renderingFrame() const -> const RenderFrame* {
		return m_rendering_frame;
	}

	[[nodiscard]]
	auto getRenderDocAPI() const noexcept -> const RENDERDOC_API_1_6_0* {
		return m_core->getRenderDocAPI();
	}

	[[nodiscard]]
	auto getOutputTarget() const noexcept -> const IOutputTarget& {
		return *m_output_target;
	}

	static VulkanRenderer* instance;

private:
	void drawFrame(RenderFrame& frame_data);

	void mainRenderThread();

	std::atomic_bool m_running {false};

	std::thread m_render_thread;

	std::array<RenderFrame, k_render_frames> m_render_frames;
	std::atomic<uint32_t> m_write_index = 0;
	std::atomic<uint32_t> m_read_index = 0;

	std::mutex m_queue_mutex;

	std::condition_variable m_frame_cv;

	std::queue<uint32_t> m_ready_frames;
	RenderFrame m_cached_frame;
	bool m_has_cached_frame = false;
	const RenderFrame* m_rendering_frame = nullptr;

	std::counting_semaphore<k_render_frames> m_free_frames {k_render_frames};

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
	void createDefaultTexture();

	/// Creates a MaterialPass for every root material present in the frame
	void ensureMaterialPasses(RenderFrame& frame_data);

	void recordFrame(FrameContext& frame, uint32_t image_index) noexcept;

	// Resource uploading
	struct BatchedUploadGroup {
		std::vector<std::unique_ptr<PendingResourceUpload>> jobs;
		vk::raii::Fence completion_fence = nullptr;
	};

	std::vector<std::unique_ptr<PendingResourceUpload>> m_upload_staging;
	std::queue<BatchedUploadGroup> m_pending_uploads;
	void processPendingUploads();
	void flushResourceUploads();
	std::mutex m_upload_mutex;

	// queueResourceUpload() offloads PendingResourceUpload::build() to the thread pool, and that job
	// captures m_core by raw pointer. Tracks how many such jobs are still in flight so stop() can
	// wait for them before returning
	std::atomic<int> m_pending_upload_builds {0};

	const VulkanCore* m_core = nullptr;

	std::unique_ptr<IOutputTarget> m_output_target;
	std::vector<std::unique_ptr<IRenderPass>> m_render_passes;

	/// One pass per root material
	std::unordered_map<assets::Material*, std::unique_ptr<MaterialPass>> m_material_passes;
	/// Guards m_material_passes + m_render_passes against listPasses()/setPassEnabled() from other threads
	std::mutex m_pass_mutex;
	/// Set by ClearUnusedAssets
	std::atomic_bool m_pending_material_pass_clear {false};
	event::Listener m_asset_listener;

	/// Fallback material for meshes without one
	assets::Handle<assets::Material> m_default_material;
	bool m_default_material_warned = false;

	/// 1x1 white texture + sampler shared by every material pass as texture fallback
	VulkanTexture m_default_texture;
	vk::raii::Sampler m_default_sampler = nullptr;
	vk::Format m_depth_format = vk::Format::eUndefined;
	DepthResources m_depth_resources;
	vk::ImageLayout m_depth_layout = vk::ImageLayout::eUndefined;

	vk::raii::CommandPool m_command_pool = nullptr;

	vk::raii::CommandPool m_transfer_command_pool = nullptr;

	TracyVkCtx m_tracy_vk_ctx = nullptr;    ///< Tracy GPU profiling context

	vk::raii::DescriptorPool m_descriptor_pool = nullptr;

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_render_finished_per_image;
	std::vector<vk::Fence> m_images_in_flight;
	std::vector<vk::ImageLayout> m_output_image_layouts;
	uint32_t m_current_frame = 0;

	/// Active camera for the renderer, Can be nullptr if no camera is set
	toast::Camera* m_camera = nullptr;

	UIFrameBuilder m_ui_frame_builder;

	std::mutex m_mesh_proxy_mutex;
	std::vector<toast::MeshNode*> m_mesh_proxy_nodes;

	// FrameUBO and related resources
	std::vector<FrameUBO> m_frame_ubos;
	std::vector<FrameResources> m_frame_ubo_res;

	void createFrameResources();
	void updateFrameResources(uint32_t frame_index, RenderFrame& frame_data);

	void applyResizeInternal(vk::Extent2D extent);

	static constexpr uint64_t k_no_pending_resize = 0;
	std::atomic<uint64_t> m_pending_resize_packed {k_no_pending_resize};

	std::atomic<double> m_frame_rate_limit_hz {0.0};
};

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

inline auto getActiveCamera() -> toast::Camera* {
	return VulkanRenderer::instance->getActiveCamera();
}

inline void setActiveCamera(toast::Camera* camera) {
	VulkanRenderer::instance->setActiveCamera(camera);
}

inline void registerMeshNodeProxy(toast::MeshNode* node) {
	VulkanRenderer::instance->registerMeshNodeProxy(node);
}

inline void unregisterMeshNodeProxy(toast::MeshNode* node) {
	VulkanRenderer::instance->unregisterMeshNodeProxy(node);
}

inline void queueResourceUpload(std::unique_ptr<PendingResourceUpload> upload) {
	VulkanRenderer::instance->queueResourceUpload(std::move(upload));
}

inline void applyResize(vk::Extent2D extent) {
	VulkanRenderer::instance->applyResize(extent);
}

inline auto getOutputTarget() -> const IOutputTarget& {
	return VulkanRenderer::instance->getOutputTarget();
}

inline auto getCore() -> const VulkanCore& {
	return VulkanRenderer::instance->getCore();
}

inline auto getRenderDocAPI() -> const RENDERDOC_API_1_6_0* {
	return VulkanRenderer::instance->getRenderDocAPI();
}

//@WARN NOT THREAD SAFE
inline auto renderingFrame() -> const VulkanRenderer::RenderFrame* {
	return VulkanRenderer::instance->renderingFrame();
}

/// DEBUG LINES

/**
 * @brief Queues a debug line segmentfor the frame currently being built
 * @note Call between beginFrameBuild() and submitFrame()
 */
inline void debugDrawLine(glm::vec3 a, glm::vec3 b, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) {
	auto& frame = VulkanRenderer::instance->beginFrameBuild();
	frame.debug_line_vertices.push_back({a, color});
	frame.debug_line_vertices.push_back({b, color});
}

/// @brief Queues a wireframe axis-aligned box for this frame
inline void debugDrawBox(glm::vec3 min, glm::vec3 max, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) {
	const std::array<glm::vec3, 8> corners {
	  glm::vec3 {min.x, min.y, min.z},
	  glm::vec3 {max.x, min.y, min.z},
	  glm::vec3 {max.x, max.y, min.z},
	  glm::vec3 {min.x, max.y, min.z},
	  glm::vec3 {min.x, min.y, max.z},
	  glm::vec3 {max.x, min.y, max.z},
	  glm::vec3 {max.x, max.y, max.z},
	  glm::vec3 {min.x, max.y, max.z},
	};
	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		debugDrawLine(corners[a], corners[b], color);
	}
}

/// @brief Queues a wireframe sphere for this frame
inline void debugDrawSphere(glm::vec3 center, float radius, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, int segments = 24) {
	for (int axis = 0; axis < 3; ++axis) {
		glm::vec3 prev {};
		for (int i = 0; i <= segments; ++i) {
			const float t = (static_cast<float>(i) / static_cast<float>(segments)) * glm::two_pi<float>();
			glm::vec3 p {};
			switch (axis) {
				case 0: p = center + glm::vec3(0.0f, std::cos(t), std::sin(t)) * radius; break;
				case 1: p = center + glm::vec3(std::cos(t), 0.0f, std::sin(t)) * radius; break;
				default: p = center + glm::vec3(std::cos(t), std::sin(t), 0.0f) * radius; break;
			}
			if (i > 0) {
				debugDrawLine(prev, p, color);
			}
			prev = p;
		}
	}
}

/// @brief Queues an axis-triad gizmo at @p transform
inline void debugDrawAxes(const glm::mat4& transform) {
	VulkanRenderer::instance->beginFrameBuild().debug_gizmo_instances.push_back(transform);
}

/**
 * @brief Queues a wireframe frustum for @p camera
 *
 * Built from the camera own fov/near/far and view matrix rather than by un-projecting NDC corners
 */
void debugDrawFrustum(const toast::Camera& camera, float aspect, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f});

}    // namespace renderer
