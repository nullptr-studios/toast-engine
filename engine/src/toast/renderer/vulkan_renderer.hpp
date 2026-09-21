/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026

#pragma once

#include "compute_pass_base.hpp"
#include "gpu_timer.hpp"
#include "output_target_base.hpp"
#include "post_process_pass_base.hpp"
#include "post_process_settings.hpp"
#include "render_pass_base.hpp"
#include "shader_layout.hpp"
#include "shadow_constants.hpp"
#include "voxel_gpu_storage.hpp"
#include "vulkan_core.hpp"
#include "vulkan_mesh.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_texture.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <toast/assets/core_types.hpp>
#include <toast/assets/mesh.hpp>
#include <toast/assets/texture.hpp>
#include <toast/events/event.inl>
#include <toast/events/listener.hpp>
#include <toast/world/gizmo_layout.hpp>
#include <tracy/TracyVulkan.hpp>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace assets {
class Material;
}

namespace toast {
class Camera;
class MeshNode;
class VoxelNode;
class Light;
class ReflectionProbe;
class IrradianceVolume;
class PostProcessVolume;
class INodeOwner;
class Node3D;
}

namespace renderer {

class MaterialPass;
class ClusterLightingPass;
class ShadowPass;
class EnvironmentPass;
class ReflectionProbePass;
class DepthPrepass;
class SkinningPass;
class RayTracingScene;
class SkinnedBlasPool;

class VulkanRenderer {
public:
	[[nodiscard]]
	static auto selectDepthFormat(const VulkanCore& core) -> vk::Format;

	static constexpr uint32_t k_frames_in_flight = 3;

	static constexpr uint8_t k_render_frames = 3;

	struct FrameContext {
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::CommandBuffer compute_command_buffer = nullptr;
		vk::raii::Semaphore image_available = nullptr;
		vk::raii::Semaphore compute_to_graphics = nullptr;
		vk::raii::Fence in_flight = nullptr;
		vk::raii::Fence compute_in_flight = nullptr;
		uint32_t last_image_index = 0;
		bool has_submitted = false;
		bool was_probe_capture = false;

		std::vector<vma::raii::Buffer> blas_scratch;
	};

	static constexpr uint32_t k_max_directional_lights = 4;

	static constexpr uint32_t k_max_joint_matrices = 4096;

	static constexpr uint32_t k_max_instances = 16384;

	/// Mirrors mesh.slang InstanceData
	struct InstanceData {
		glm::mat4 model {1.0f};
		uint32_t joint_offset = 0;
		/// Scalars not uint3 since std430 aligns uint3 to 16 bytes
		std::array<uint32_t, 3> _pad0 {0, 0, 0};
	};

	static_assert(sizeof(InstanceData) == 80, "InstanceData stride must match mesh.slang's std430 layout");

	struct DirectionalLightData {
		glm::vec4 direction;
		glm::vec4 color_intensity;
	};

	static constexpr uint32_t k_max_reflection_probes = 8;

	struct ReflectionProbeData {
		glm::vec4 position_radius;
		// xyz box half extents w intensity. Zero extents use sphere projection
		glm::vec4 box_extents_intensity;

		// x cubemap index in registration order
		glm::vec4 params {0.0f};
	};

	static constexpr uint32_t k_max_irradiance_volumes = 4;

	static constexpr uint32_t k_max_irradiance_probes = 4096;

	static constexpr uint32_t k_irradiance_capture_size = 64;

	struct IrradianceVolumeData {
		/// xyz grid min corner w probe spacing
		glm::vec4 min_corner_spacing {0.0f};
		/// xyz probe counts w first probe index in the SH buffer
		glm::uvec4 counts_base {0};
		/// xyz half extents w intensity
		glm::vec4 extents_intensity {0.0f};

		/// x 1 once the SH slice of this volume is baked
		glm::uvec4 baked_pad {0};
	};

	using PostProcessSettings = renderer::PostProcessSettings;

	struct FrameUBO {
		glm::mat4 view {1.0f};
		glm::mat4 projection {1.0f};
		glm::mat4 view_projection {1.0f};

		glm::vec3 camera_position {};

		float time = 0.0f;

		glm::vec4 ambient_color_intensity {0.0f};

		// uvec4 so std140 and cbuffer packing agree
		glm::uvec4 directional_light_count_pad {0};

		std::array<DirectionalLightData, k_max_directional_lights> directional_lights {};

		glm::uvec4 render_mode_pad {0};

		std::array<glm::mat4, shadows::k_cascade_count> cascade_view_projection {};

		glm::vec4 cascade_splits {0.0f};

		glm::vec4 cascade_texel_world_size {0.0f};

		glm::vec4 cascade_depth_bias {0.0f};

		// x caster index + 1 (0 none) y cascades fitted zw texel size in UV
		glm::vec4 shadow_params {0.0f};

		// x 1 when the cubemaps hold data y prefiltered roughness levels
		glm::vec4 environment_params {0.0f};

		glm::uvec4 reflection_probe_count_pad {0};

		std::array<ReflectionProbeData, k_max_reflection_probes> reflection_probes {};

		glm::uvec4 irradiance_volume_count_pad {0};

		std::array<IrradianceVolumeData, k_max_irradiance_volumes> irradiance_volumes {};

		// x 1 traces the TLAS instead of sampling shadow maps
		glm::vec4 traced_shadow_params {0.0f};
	};

	// Verify offsets with slangc -target spirv-assembly | grep OpMemberDecorate
	static_assert(
	    offsetof(FrameUBO, irradiance_volumes) == 1136, "FrameUBO::irradiance_volumes must match lighting.slang's std140 layout"
	);
	static_assert(
	    offsetof(FrameUBO, traced_shadow_params) == 1392, "FrameUBO::traced_shadow_params must match lighting.slang's std140 layout"
	);

	/// Mirrors GpuLight in lighting.slang and cluster_lighting.slang
	struct GpuLight {
		glm::vec4 world_pos_range;
		glm::vec4 view_pos_type;    // w type 0 point 1 spot
		glm::vec4 color_intensity;
		glm::vec4 direction_pad;    // w shadow layer or -1
		glm::vec4 cone_angles;      // x cos outer y cos inner z shadow near w shadow far

		// x fraction of the layer rendered y point light face scale inside the guard band
		glm::vec4 shadow_atlas {1.0f, 1.0f, 0.0f, 0.0f};

		glm::mat4 shadow_view_projection {1.0f};
	};

	struct ShadowView {
		glm::mat4 view_projection {1.0f};

		glm::vec4 cull_sphere {0.0f};

		uint32_t layer = 0;
		bool directional = false;

		uint32_t resolution = 0;
	};

	struct ShadowFrame {
		std::vector<ShadowView> views;

		/// matrices[i] belongs to views[i]
		std::vector<glm::mat4> matrices;
	};

	struct MeshInstanceProxy {
		VulkanMesh* mesh = nullptr;
		assets::Material* material = nullptr;
		assets::Material* root_material = nullptr;
		glm::mat4 model = glm::mat4(1.0f);

		// joint_count > 0 selects the skinned pipeline
		uint32_t joint_offset = 0;
		uint32_t joint_count = 0;

		glm::vec3 bounds_center {0.0f};
		float bounds_radius = 0.0f;

		bool visible = true;

		uint32_t instance_index = 0;

		static constexpr uint32_t k_no_posed_vertices = ~0u;

		uint32_t posed_vertex_offset = k_no_posed_vertices;

		uint64_t node_uid = 0;
	};

	struct VoxelVolumeProxy {
		uint32_t record_index = 0;

		glm::uvec3 brick_dims {0};

		glm::mat4 model {1.0f};
		glm::mat4 inverse_model {1.0f};

		glm::mat4 previous_model {1.0f};

		glm::vec3 bounds_center {0.0f};
		float bounds_radius = 0.0f;

		float view_distance = 0.0f;

		bool camera_inside = false;

		bool mirrored = false;

		bool visible = true;

		uint64_t node_uid = 0;
	};

	struct UIWorldPanelProxy {
		vk::ImageView view = nullptr;
		glm::mat4 model = glm::mat4(1.0f);
	};

	struct DebugVertex {
		glm::vec<3, float, glm::packed_highp> position;
		glm::vec<4, float, glm::packed_highp> color;
	};

	static_assert(std::is_standard_layout_v<DebugVertex>, "DebugVertex must be standard layout");

	struct DebugBillboard {
		glm::vec3 position {0.0f};
		float size = 1.0f;
		glm::vec4 tint {1.0f};
		assets::Handle<assets::Texture> texture;
	};

	struct DebugMesh {
		glm::mat4 model {1.0f};
		glm::vec4 tint {1.0f};
		assets::Handle<assets::Mesh> mesh;
	};

	struct TransformGizmoDraw {
		bool visible = false;
		toast::GizmoTool tool = toast::GizmoTool::select;
		glm::mat4 model {1.0f};
		toast::GizmoHandle hover = toast::GizmoHandle::none;
		toast::GizmoHandle active = toast::GizmoHandle::none;
		float drag_scale_factor = 1.0f;
	};

	struct GizmoState {
		bool visible = false;
		toast::GizmoTool tool = toast::GizmoTool::select;
		glm::vec3 origin {0.0f};
		glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
		toast::GizmoHandle hover = toast::GizmoHandle::none;
		toast::GizmoHandle active = toast::GizmoHandle::none;
		float drag_scale_factor = 1.0f;
	};

	struct ImGuiKeyEvent {
		int32_t key = 0;
		bool down = false;
	};

	struct ImGuiInputSnapshot {
		glm::vec2 mouse_pos {-1.0f, -1.0f};    // -1 -1 when outside the window
		std::array<bool, 3> mouse_down {};     // 0 left 1 right 2 middle
		float mouse_wheel_x = 0.0f;
		float mouse_wheel_y = 0.0f;
		std::vector<ImGuiKeyEvent> key_events;
		std::vector<uint32_t> char_events;
	};

	struct RenderFrame {
		FrameUBO frame_data;

		std::vector<MeshInstanceProxy> mesh_instances;

		/// Sorted front to back
		std::vector<VoxelVolumeProxy> voxel_instances;

		std::shared_ptr<const VoxelGpuStorage> voxel_storage;

		std::vector<assets::HandleBase> asset_refs;

		std::vector<GpuLight> lights;

		ShadowFrame shadows;

		std::vector<glm::mat4> joint_matrices;

		std::vector<InstanceData> instance_data;

		std::vector<InstanceData> shadow_instance_data;

		struct MaterialRange {
			assets::Material* root_material = nullptr;
			uint32_t begin = 0;
			uint32_t end = 0;
		};

		std::vector<MaterialRange> material_ranges;

		glm::vec2 viewport_extent {0.0f};
		float camera_near = 0.01f;
		float camera_far = 5000.0f;

		ImGuiInputSnapshot imgui_input;

		uint32_t render_mode = 0;

		uint64_t sequence = 0;

		PostProcessSettings post_process;

		int32_t probe_capture_index = -1;
		uint32_t probe_capture_face = 0;

		int32_t irradiance_capture_index = -1;
		uint32_t irradiance_capture_face = 0;

		uint32_t capture_extent = 0;

		std::vector<DebugVertex> debug_line_vertices;    // pairs form line segments
		std::vector<DebugVertex> debug_triangle_vertices;
		std::vector<glm::mat4> debug_gizmo_instances;
		std::vector<DebugBillboard> debug_billboards;
		std::vector<DebugMesh> debug_meshes;

		TransformGizmoDraw transform_gizmo;

		std::vector<vk::CommandBuffer> ui_command_buffers;
		std::vector<vk::ImageView> ui_output_views;
		std::vector<UIWorldPanelProxy> ui_world_panels;
		std::shared_ptr<const void> ui_slot_guard;
	};

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

	void submitFrame() noexcept;

	void tick(float time) noexcept;

	void registerMeshNodeProxy(toast::MeshNode* node);
	void registerDebugDraw(toast::Node3D* node, void (*draw)(toast::Node3D&));
	void unregisterDebugDraw(toast::Node3D* node);

	void unregisterMeshNodeProxy(toast::MeshNode* node);

	void registerVoxelNodeProxy(toast::VoxelNode* node);

	void unregisterVoxelNodeProxy(toast::VoxelNode* node);

	void registerLightNodeProxy(toast::Light* node);

	void unregisterLightNodeProxy(toast::Light* node);

	void registerCameraNodeProxy(toast::Camera* node);

	void unregisterCameraNodeProxy(toast::Camera* node);

	void registerReflectionProbeProxy(toast::ReflectionProbe* node);

	void unregisterReflectionProbeProxy(toast::ReflectionProbe* node);

	void registerIrradianceVolumeProxy(toast::IrradianceVolume* node);

	void registerPostProcessVolumeProxy(toast::PostProcessVolume* node);

	void unregisterPostProcessVolumeProxy(toast::PostProcessVolume* node);

	void unregisterIrradianceVolumeProxy(toast::IrradianceVolume* node);

private:
	void cancelIrradianceBake();

public:
	/// 0 or negative is uncapped
	void setFrameRateLimit(double max_fps) noexcept { m_frame_rate_limit_hz.store(max_fps, std::memory_order_relaxed); }

	[[nodiscard]]
	auto frameRateLimit() const noexcept -> double {
		return m_frame_rate_limit_hz.load(std::memory_order_relaxed);
	}

	static constexpr double k_background_frame_rate_limit = 30.0;

	/// False repeats the last frame instead of waiting for a new one
	void setClampToSimulation(bool clamp) noexcept { m_clamp_to_simulation.store(clamp, std::memory_order_relaxed); }

	[[nodiscard]]
	auto clampToSimulation() const noexcept -> bool {
		return m_clamp_to_simulation.load(std::memory_order_relaxed);
	}

	void setApplicationFocused(bool focused) noexcept { m_application_focused.store(focused, std::memory_order_relaxed); }

	[[nodiscard]]
	auto applicationFocused() const noexcept -> bool {
		return m_application_focused.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto effectiveFrameRateLimit() const noexcept -> double {
		const double user_limit = frameRateLimit();
		if (applicationFocused()) {
			return user_limit;
		}
		return user_limit > 0.0 && user_limit < k_background_frame_rate_limit ? user_limit : k_background_frame_rate_limit;
	}

	void setRenderingPaused(bool paused) noexcept;

	[[nodiscard]]
	auto renderingPaused() const noexcept -> bool {
		return m_rendering_paused.load(std::memory_order_relaxed);
	}

	void stop();

	void addRenderPass(std::unique_ptr<IRenderPass> pass);

	void addComputePass(std::unique_ptr<IComputePass> pass);

	/// Runs in registration order
	void addPostProcessPass(std::unique_ptr<IPostProcessPass> pass);

	auto setPostProcessPassEnabled(std::string_view name, bool enabled) -> bool;

	[[nodiscard]]
	auto isPostProcessPassEnabled(std::string_view name) const -> bool;

	[[nodiscard]]
	auto getSceneColorFormat() const noexcept -> vk::Format {
		return m_scene_color_format;
	}

	[[nodiscard]]
	auto getSceneColorView() const noexcept -> vk::ImageView {
		return m_scene_color.view.has_value() ? **m_scene_color.view : vk::ImageView {};
	}

	/// xyz world normal w roughness

	[[nodiscard]]
	auto getSceneNormalView() const noexcept -> vk::ImageView {
		return m_scene_normal.view.has_value() ? **m_scene_normal.view : vk::ImageView {};
	}

	[[nodiscard]]
	auto getSceneIndirectView() const noexcept -> vk::ImageView {
		return m_scene_indirect.view.has_value() ? **m_scene_indirect.view : vk::ImageView {};
	}

	/// Left in eDepthReadOnlyOptimal
	[[nodiscard]]
	auto getDepthView() const noexcept -> vk::ImageView {
		return m_depth_resources.view.has_value() ? **m_depth_resources.view : vk::ImageView {};
	}

	struct PassInfo {
		std::string name;
		bool enabled = true;
	};

	[[nodiscard]]
	auto listPasses() -> std::vector<PassInfo>;

	void setPassEnabled(std::string_view name, bool enabled);

	[[nodiscard]]
	auto getDefaultTextureView() const noexcept -> vk::ImageView {
		return m_default_texture.getView();
	}

	[[nodiscard]]
	auto getFailsafeTextureView(bool has_reference, const VulkanTexture* texture) const noexcept -> vk::ImageView;

	[[nodiscard]]
	auto getFailsafeSampler() const noexcept -> vk::Sampler {
		return *m_failsafe_sampler;
	}

	[[nodiscard]]
	auto getDefaultBlackTextureView() const noexcept -> vk::ImageView {
		return m_default_black_texture.getView();
	}

	[[nodiscard]]
	auto getDefaultNormalTextureView() const noexcept -> vk::ImageView {
		return m_default_normal_texture.getView();
	}

	[[nodiscard]]
	auto getDefaultSampler() const noexcept -> vk::Sampler {
		return *m_default_sampler;
	}

	[[nodiscard]]
	auto getDefaultShadowMapView() const noexcept -> vk::ImageView {
		return *m_default_shadow_view;
	}

	[[nodiscard]]
	auto getDefaultShadowSampler() const noexcept -> vk::Sampler {
		return *m_default_shadow_sampler;
	}

	[[nodiscard]]
	auto getDefaultCubeView() const noexcept -> vk::ImageView {
		return *m_default_cube_view;
	}

	void applyResize(vk::Extent2D extent);

	void queueResourceUpload(std::unique_ptr<PendingResourceUpload> upload);

	[[nodiscard]]
	auto getFrameUBORes(uint32_t current_frame) const -> const FrameResources* {
		return &m_frame_ubo_res[current_frame];
	}

	[[nodiscard]]
	auto getInstanceBuffer(uint32_t frame_index) const -> vk::Buffer {
		return frame_index < m_instance_res.size() && m_instance_res[frame_index].gpu_buffer.has_value()
		           ? **m_instance_res[frame_index].gpu_buffer
							 : vk::Buffer {};
	}

	[[nodiscard]]
	auto getShadowInstanceBuffer(uint32_t frame_index) const -> vk::Buffer {
		return frame_index < m_shadow_instance_res.size() && m_shadow_instance_res[frame_index].gpu_buffer.has_value()
		           ? **m_shadow_instance_res[frame_index].gpu_buffer
							 : vk::Buffer {};
	}

	[[nodiscard]]
	auto getJointMatrixBuffer(uint32_t frame_index) const -> vk::Buffer {
		return frame_index < m_joint_matrix_res.size() && m_joint_matrix_res[frame_index].gpu_buffer.has_value()
		           ? **m_joint_matrix_res[frame_index].gpu_buffer
							 : vk::Buffer {};
	}

	[[nodiscard]]
	auto getDescriptorPoolHandle() const noexcept -> vk::DescriptorPool {
		return *m_descriptor_pool;
	}

	void setClusterLightingPass(const ClusterLightingPass* pass) noexcept { m_cluster_lighting_pass = pass; }

	[[nodiscard]]
	auto getClusterLightingPass() const noexcept -> const ClusterLightingPass* {
		return m_cluster_lighting_pass;
	}

	void setShadowPass(const ShadowPass* pass) noexcept { m_shadow_pass = pass; }

	[[nodiscard]]
	auto getShadowPass() const noexcept -> const ShadowPass* {
		return m_shadow_pass;
	}

	void setEnvironmentPass(EnvironmentPass* pass) noexcept { m_environment_pass = pass; }

	[[nodiscard]]
	auto getEnvironmentPass() const noexcept -> const EnvironmentPass* {
		return m_environment_pass;
	}

	void setReflectionProbePass(ReflectionProbePass* pass) noexcept { m_reflection_probe_pass = pass; }

	[[nodiscard]]
	auto getReflectionProbePass() const noexcept -> ReflectionProbePass* {
		return m_reflection_probe_pass;
	}

	void requestReflectionProbeBake() noexcept { m_probe_bake_requested.store(true, std::memory_order_release); }

	void requestIrradianceBake() noexcept { m_irradiance_bake_requested.store(true, std::memory_order_release); }

	[[nodiscard]]
	auto getIrradianceProbeCount() const noexcept -> uint32_t {
		return m_irradiance_probe_total;
	}

	[[nodiscard]]
	auto getStaleIrradianceVolumeCount() const noexcept -> uint32_t {
		return m_irradiance_stale_count.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto getOutOfOrderFrameCount() const noexcept -> uint32_t {
		return m_out_of_order_frames.load(std::memory_order_relaxed);
	}

	void getFrameTimeStats(float& avg_ms, float& min_ms, float& max_ms) const noexcept {
		avg_ms = m_frame_time_avg_ms.load(std::memory_order_relaxed);
		min_ms = m_frame_time_min_ms.load(std::memory_order_relaxed);
		max_ms = m_frame_time_max_ms.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto getSkippedBuildCount() const noexcept -> uint32_t {
		return m_skipped_builds.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto getDroppedFrameCount() const noexcept -> uint32_t {
		return m_dropped_frames.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto getVisibleInstanceCount() const noexcept -> uint32_t {
		return m_visible_instance_count.load(std::memory_order_relaxed);
	}

	void setCullDebugDraw(bool enabled) noexcept { m_cull_debug_draw.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto getCullDebugDraw() const noexcept -> bool {
		return m_cull_debug_draw.load(std::memory_order_relaxed);
	}

	void setCullFreeze(bool frozen) noexcept { m_cull_freeze.store(frozen, std::memory_order_relaxed); }

	[[nodiscard]]
	auto getCullFreeze() const noexcept -> bool {
		return m_cull_freeze.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto getStaleReflectionProbeCount() const noexcept -> uint32_t {
		return m_probe_stale_count.load(std::memory_order_relaxed);
	}

	void requestMaterialFrameSetRebuild();

	/// @warning Render thread only
	[[nodiscard]]
	auto getEnvironmentPassMutable() const noexcept -> EnvironmentPass* {
		return m_environment_pass;
	}

	[[nodiscard]]
	auto getRayTracingScene() const noexcept -> RayTracingScene* {
		return m_ray_tracing_scene.get();
	}

	[[nodiscard]]
	auto getPrepassDrawnCount() const noexcept -> uint32_t;

	[[nodiscard]]
	auto getSkinningPass() const noexcept -> SkinningPass* {
		return m_skinning_pass.get();
	}

	[[nodiscard]]
	auto getPosedVertexBuffer(uint32_t frame_index) const -> vk::Buffer;

	[[nodiscard]]
	auto getSkinnedBlasPool() const noexcept -> SkinnedBlasPool* {
		return m_skinned_blas_pool.get();
	}

	[[nodiscard]]
	auto materialUsesCutout(assets::Material* material) const -> bool;

	[[nodiscard]]
	auto getReflectionProbePassMutable() const noexcept -> ReflectionProbePass* {
		return m_reflection_probe_pass;
	}

	[[nodiscard]]
	void setActiveCamera(toast::Camera* camera);

	void forgetCamera(const toast::Camera* camera);

	[[nodiscard]]
	auto getCore() -> const VulkanCore& {
		return *m_core;
	}

	[[nodiscard]]
	auto getActiveCamera() -> toast::Camera* {
		return m_camera;
	}

	void setGizmoState(const GizmoState& state) noexcept { m_gizmo_state = state; }

	void setDebugDrawEnabled(bool enabled) noexcept { m_debug_draw_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto debugDrawEnabled() const noexcept -> bool {
		return m_debug_draw_enabled.load(std::memory_order_relaxed);
	}

	struct PerfCounters {
		uint64_t ticks = 0;
		uint64_t frames_built = 0;
		uint64_t slot_wait_ns = 0;
		uint64_t draws = 0;
		uint64_t repeated_draws = 0;
		uint64_t draw_work_ns = 0;

		uint64_t upload_ns = 0;
		uint64_t gpu_wait_ns = 0;
		uint64_t acquire_ns = 0;
		uint64_t record_ns = 0;
		uint64_t submit_ns = 0;
		uint64_t present_ns = 0;
		uint64_t frame_wait_ns = 0;
		uint64_t pacing_ns = 0;
		uint64_t build_ns = 0;
	};

	[[nodiscard]]
	auto perfCounters() const noexcept -> PerfCounters {
		return {
		  .ticks = m_perf_ticks.load(std::memory_order_relaxed),
		  .frames_built = m_perf_frames_built.load(std::memory_order_relaxed),
		  .slot_wait_ns = m_perf_slot_wait_ns.load(std::memory_order_relaxed),
		  .draws = m_perf_draws.load(std::memory_order_relaxed),
		  .repeated_draws = m_perf_repeated_draws.load(std::memory_order_relaxed),
		  .draw_work_ns = m_perf_draw_work_ns.load(std::memory_order_relaxed),
		  .upload_ns = m_perf_upload_ns.load(std::memory_order_relaxed),
		  .gpu_wait_ns = m_perf_gpu_wait_ns.load(std::memory_order_relaxed),
		  .acquire_ns = m_perf_acquire_ns.load(std::memory_order_relaxed),
		  .record_ns = m_perf_record_ns.load(std::memory_order_relaxed),
		  .submit_ns = m_perf_submit_ns.load(std::memory_order_relaxed),
		  .present_ns = m_perf_present_ns.load(std::memory_order_relaxed),
		  .frame_wait_ns = m_perf_frame_wait_ns.load(std::memory_order_relaxed),
		  .pacing_ns = m_perf_pacing_ns.load(std::memory_order_relaxed),
		  .build_ns = m_perf_build_ns.load(std::memory_order_relaxed),
		};
	}

	/// @note Render thread only
	[[nodiscard]]
	auto gpuTimer() const noexcept -> const GpuTimer* {
		return m_gpu_timer.get();
	}

	void setTracedShadowsEnabled(bool enabled) noexcept { m_traced_shadows_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto tracedShadowsEnabled() const noexcept -> bool {
		return m_traced_shadows_enabled.load(std::memory_order_relaxed);
	}

	void setPostProcessSettings(const PostProcessSettings& settings) noexcept { m_post_process_settings = settings; }

	[[nodiscard]]
	auto getPostProcessSettings() const noexcept -> const PostProcessSettings& {
		return m_post_process_settings;
	}

	/// Safe from any thread
	void requestPostProcessSettings(const PostProcessSettings& settings) {
		const std::lock_guard lock(m_pending_post_settings_mutex);
		m_pending_post_settings = settings;
	}

	/// nullptr renders every owner. Main thread only
	void setRenderOwnerFilter(const toast::INodeOwner* owner) {
		if (m_render_owner_filter == owner) {
			return;
		}
		m_render_owner_filter = owner;
		cancelIrradianceBake();
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

	std::mutex m_queue_mutex;

	std::condition_variable m_frame_cv;

	std::queue<uint32_t> m_ready_frames;
	const RenderFrame* m_rendering_frame = nullptr;

	std::counting_semaphore<k_render_frames> m_free_frames {k_render_frames};

	struct DepthResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
	};

	struct SceneColorResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		vk::ImageLayout layout = vk::ImageLayout::eUndefined;
	};

	void createGraphicsCommandPool();
	void createTransferCommandPool();
	void createComputeCommandPool();
	void createFrameContexts();

	void createPerImageSync();
	void createDepthResources();
	void createSceneColorResources();

	void publishCompletedFrames();

	void recordAccelerationStructureBuilds(FrameContext& frame);

	void createPresentResources();
	void createDescriptorPool();
	void createDefaultTexture();

	void createFailsafeTextures();
	void createDefaultShadowMap();
	void createDefaultCubemap();

	void ensureMaterialPasses(RenderFrame& frame_data);

	void recordFrame(FrameContext& frame, uint32_t image_index) noexcept;

	/// @note Expects an open rendering scope with m_pass_mutex held
	void recordMeshScene(vk::CommandBuffer cmd, uint32_t image_index);

	struct PunctualShadowCandidate {
		size_t light_index = 0;
		float distance_squared = 0.0f;
		float resolution_scale = 1.0f;
	};

	void fitShadowViews(
	    RenderFrame& frame, float aspect, int32_t shadow_caster_index, const glm::vec3& shadow_caster_direction,
	    std::vector<PunctualShadowCandidate>& spot_candidates, std::vector<PunctualShadowCandidate>& point_candidates
	);

	struct UploadSlot {
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::Fence fence = nullptr;
		bool in_flight = false;
	};

	static constexpr uint32_t k_upload_slots = 4;

	struct BatchedUploadGroup {
		std::vector<std::unique_ptr<PendingResourceUpload>> jobs;
		uint32_t slot = 0;
	};

	vk::raii::CommandPool m_command_pool = nullptr;
	vk::raii::CommandPool m_transfer_command_pool = nullptr;
	vk::raii::CommandPool m_compute_command_pool = nullptr;
	vk::raii::DescriptorPool m_descriptor_pool = nullptr;

	std::vector<UploadSlot> m_upload_slots;
	uint32_t m_next_upload_slot = 0;

	static constexpr vk::DeviceSize k_upload_host_budget = 256ull << 20;

	/// Guarded by m_upload_mutex
	std::deque<std::unique_ptr<PendingResourceUpload>> m_upload_waiting;
	std::atomic<vk::DeviceSize> m_upload_host_bytes {0};

	std::vector<std::unique_ptr<PendingResourceUpload>> m_upload_staging;
	std::queue<BatchedUploadGroup> m_pending_uploads;
	void createUploadRing();
	void pumpUploadQueue();
	void processPendingUploads();
	void flushResourceUploads();

	/// Main thread only
	void buildVoxelProxies(RenderFrame& frame);
	std::mutex m_upload_mutex;

	std::atomic<int> m_pending_upload_builds {0};

	const VulkanCore* m_core = nullptr;

	std::unique_ptr<IOutputTarget> m_output_target;
	std::vector<std::unique_ptr<IRenderPass>> m_render_passes;
	std::vector<std::unique_ptr<IComputePass>> m_compute_passes;
	std::vector<std::unique_ptr<IPostProcessPass>> m_post_process_passes;

	std::unordered_map<assets::Material*, std::unique_ptr<MaterialPass>> m_material_passes;
	/// Guards m_material_passes and m_render_passes
	mutable std::mutex m_pass_mutex;
	std::atomic_bool m_pending_material_pass_clear {false};
	event::Listener m_asset_listener;

	assets::Handle<assets::Material> m_default_material;
	bool m_default_material_warned = false;

	VulkanTexture m_default_texture;
	VulkanTexture m_fail_load_texture;
	VulkanTexture m_fail_gpu_texture;
	VulkanTexture m_missing_texture;
	vk::raii::Sampler m_failsafe_sampler = nullptr;
	VulkanTexture m_default_black_texture;
	VulkanTexture m_default_normal_texture;
	vk::raii::Sampler m_default_sampler = nullptr;

	std::optional<vma::raii::Image> m_default_shadow_image;
	vk::raii::ImageView m_default_shadow_view = nullptr;
	vk::raii::Sampler m_default_shadow_sampler = nullptr;

	std::optional<vma::raii::Image> m_default_cube_image;
	vk::raii::ImageView m_default_cube_view = nullptr;

	const ClusterLightingPass* m_cluster_lighting_pass = nullptr;

	const ShadowPass* m_shadow_pass = nullptr;

	EnvironmentPass* m_environment_pass = nullptr;

	ReflectionProbePass* m_reflection_probe_pass = nullptr;

	std::atomic_bool m_probe_bake_requested {false};
	std::atomic<uint32_t> m_probe_stale_count {0};
	/// probe = cursor / 6 and face = cursor % 6 or -1 when idle
	int32_t m_probe_bake_cursor = -1;
	bool m_probe_auto_baked = false;
	vk::Format m_depth_format = vk::Format::eUndefined;
	DepthResources m_depth_resources;

	static constexpr vk::Format m_scene_color_format = vk::Format::eR16G16B16A16Sfloat;
	SceneColorResources m_scene_color;

	static constexpr vk::Format m_scene_normal_format = renderer::k_scene_normal_format;
	SceneColorResources m_scene_normal;

	static constexpr vk::Format m_scene_indirect_format = renderer::k_scene_indirect_format;
	SceneColorResources m_scene_indirect;

	ShaderLayout m_present_layout;
	VulkanPipeline m_present_pipeline;
	vk::raii::Sampler m_present_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_present_sets;
	std::vector<vk::ImageView> m_present_bound_views;
	vk::ImageLayout m_depth_layout = vk::ImageLayout::eUndefined;

	TracyVkCtx m_tracy_vk_ctx = nullptr;
	std::unique_ptr<GpuTimer> m_gpu_timer;

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_render_finished_per_image;
	std::vector<vk::Fence> m_images_in_flight;
	std::vector<vk::ImageLayout> m_output_image_layouts;
	uint32_t m_current_frame = 0;

	toast::Camera* m_camera = nullptr;

	GizmoState m_gizmo_state;

	[[nodiscard]]
	auto blendPostProcessVolumes(const glm::vec3& camera_position) -> PostProcessSettings;

	PostProcessSettings m_post_process_settings;

	std::atomic_bool m_traced_shadows_enabled {false};

	std::atomic_bool m_debug_draw_enabled {false};

	std::mutex m_pending_post_settings_mutex;
	std::optional<PostProcessSettings> m_pending_post_settings;

	const toast::INodeOwner* m_render_owner_filter = nullptr;

	event::Listener m_capture_listener;
	std::atomic_bool m_capture_frame_requested {false};

	event::Listener m_imgui_input_listener;
	glm::vec2 m_imgui_mouse_pos {-1.0f, -1.0f};
	std::array<bool, 3> m_imgui_mouse_down {};
	float m_imgui_wheel_x_accum = 0.0f;
	float m_imgui_wheel_y_accum = 0.0f;
	std::vector<ImGuiKeyEvent> m_imgui_key_events_accum;
	std::vector<uint32_t> m_imgui_char_events_accum;

	event::Listener m_render_mode_listener;
	uint32_t m_render_mode = 0;

	UIFrameBuilder m_ui_frame_builder;

	std::mutex m_mesh_proxy_mutex;
	std::vector<toast::MeshNode*> m_mesh_proxy_nodes;
	std::vector<std::pair<toast::Node3D*, void (*)(toast::Node3D&)>> m_debug_nodes;

	std::mutex m_voxel_proxy_mutex;
	std::vector<toast::VoxelNode*> m_voxel_proxy_nodes;
	std::vector<toast::VoxelNode*> m_tick_voxel_nodes;

	std::shared_ptr<VoxelGpuStorage> m_voxel_storage;
	std::shared_ptr<VoxelGpuStorage> m_voxel_storage_pending;

	struct VoxelSceneKey {
		uint64_t node_uid = 0;
		uint32_t revision = 0;
		uint32_t content = 0;
		const voxel::Palette* palette = nullptr;

		[[nodiscard]]
		auto operator==(const VoxelSceneKey&) const -> bool = default;
	};

	std::vector<VoxelSceneKey> m_voxel_scene_key;

	std::unordered_map<uint64_t, glm::mat4> m_voxel_previous_models;
	bool m_voxel_upload_failed_warned = false;

	/// Toggling re-packs to add or drop the mirror
	bool m_voxel_mirror_kept = false;
	uint64_t m_voxel_upload_sequence = 0;

	std::mutex m_light_proxy_mutex;
	std::vector<toast::Light*> m_light_proxy_nodes;

	std::mutex m_camera_proxy_mutex;
	std::vector<toast::Camera*> m_camera_proxy_nodes;

	std::mutex m_reflection_probe_mutex;
	std::vector<toast::ReflectionProbe*> m_reflection_probe_nodes;

	/// Guarded by m_reflection_probe_mutex
	std::vector<toast::IrradianceVolume*> m_irradiance_volume_nodes;

	/// Guarded by m_reflection_probe_mutex
	std::vector<toast::PostProcessVolume*> m_post_process_volume_nodes;

	std::vector<toast::MeshNode*> m_tick_mesh_nodes;
	std::vector<toast::Light*> m_tick_light_nodes;
	std::vector<toast::Camera*> m_tick_camera_nodes;
	std::vector<toast::ReflectionProbe*> m_tick_probe_nodes;
	std::vector<std::pair<uint32_t, const toast::ReflectionProbe*>> m_tick_probes_by_distance;
	std::vector<PunctualShadowCandidate> m_tick_spot_candidates;
	std::vector<PunctualShadowCandidate> m_tick_point_candidates;
	std::vector<toast::PostProcessVolume*> m_tick_post_volumes;

	std::vector<toast::IrradianceVolume*> m_irradiance_published_nodes;

	uint32_t m_irradiance_probe_total = 0;

	std::unordered_set<toast::IrradianceVolume*> m_irradiance_load_attempted;

	std::unordered_map<toast::IrradianceVolume*, ShGridKey> m_irradiance_baked_keys;

	std::atomic<uint32_t> m_irradiance_stale_count {0};

	std::atomic<uint32_t> m_visible_instance_count {0};

	uint64_t m_frame_boundary_counter = 0;

	uint64_t m_frame_sequence_counter = 0;

	std::deque<uint32_t> m_pending_publish;

	std::unique_ptr<DepthPrepass> m_depth_prepass;

	std::unique_ptr<SkinningPass> m_skinning_pass;

	std::unique_ptr<RayTracingScene> m_ray_tracing_scene;

	std::unique_ptr<SkinnedBlasPool> m_skinned_blas_pool;

	std::atomic<uint64_t> m_last_drawn_sequence {0};
	std::atomic<uint32_t> m_out_of_order_frames {0};
	std::atomic<uint32_t> m_dropped_frames {0};

	std::atomic<uint32_t> m_skipped_builds {0};

	std::atomic<float> m_frame_time_avg_ms {0.0f};
	std::atomic<float> m_frame_time_min_ms {0.0f};
	std::atomic<float> m_frame_time_max_ms {0.0f};

	std::atomic<uint64_t> m_perf_ticks {0};
	std::atomic<uint64_t> m_perf_frames_built {0};
	std::atomic<uint64_t> m_perf_slot_wait_ns {0};
	std::atomic<uint64_t> m_perf_draws {0};
	std::atomic<uint64_t> m_perf_repeated_draws {0};
	std::atomic<uint64_t> m_perf_draw_work_ns {0};
	std::atomic<uint64_t> m_perf_upload_ns {0};
	std::atomic<uint64_t> m_perf_gpu_wait_ns {0};
	std::atomic<uint64_t> m_perf_acquire_ns {0};
	std::atomic<uint64_t> m_perf_record_ns {0};
	std::atomic<uint64_t> m_perf_submit_ns {0};
	std::atomic<uint64_t> m_perf_present_ns {0};
	std::atomic<uint64_t> m_perf_frame_wait_ns {0};
	std::atomic<uint64_t> m_perf_pacing_ns {0};
	std::atomic<uint64_t> m_perf_build_ns {0};

	std::atomic_bool m_cull_debug_draw {false};

	std::atomic_bool m_cull_freeze {false};
	glm::mat4 m_frozen_cull_view_projection {1.0f};
	bool m_has_frozen_cull = false;

	int32_t m_irradiance_bake_cursor = -1;
	std::atomic_bool m_irradiance_bake_requested {false};

	std::atomic_bool m_bake_active {false};
	std::unordered_set<toast::ReflectionProbe*> m_probe_load_attempted;

	std::vector<FrameUBO> m_frame_ubos;
	std::vector<FrameResources> m_frame_ubo_res;
	std::vector<FrameResources> m_joint_matrix_res;
	std::vector<FrameResources> m_instance_res;
	std::vector<FrameResources> m_shadow_instance_res;
	bool m_joint_matrix_pool_exhausted_warned = false;

	void createFrameResources();
	void updateFrameResources(uint32_t frame_index, RenderFrame& frame_data);

	void applyResizeInternal(vk::Extent2D extent);

	static constexpr uint64_t k_no_pending_resize = 0;
	std::atomic<uint64_t> m_pending_resize_packed {k_no_pending_resize};

	std::atomic<double> m_frame_rate_limit_hz {0.0};
	std::atomic_bool m_application_focused {true};
	std::atomic_bool m_rendering_paused {false};
	std::atomic_bool m_clamp_to_simulation {true};
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

inline void setActiveCamera(toast::Camera& camera) {
	VulkanRenderer::instance->setActiveCamera(&camera);
}

inline void setActiveCamera(toast::Camera* camera) {
	if (VulkanRenderer::instance != nullptr) {
		VulkanRenderer::instance->setActiveCamera(camera);
	}
}

inline void forgetCamera(const toast::Camera* camera) {
	if (VulkanRenderer::instance != nullptr) {
		VulkanRenderer::instance->forgetCamera(camera);
	}
}

inline void registerMeshNodeProxy(toast::MeshNode* node) {
	VulkanRenderer::instance->registerMeshNodeProxy(node);
}

inline void unregisterMeshNodeProxy(toast::MeshNode* node) {
	VulkanRenderer::instance->unregisterMeshNodeProxy(node);
}

inline auto registerVoxelNodeProxy(toast::VoxelNode* node) -> bool {
	if (VulkanRenderer::instance == nullptr) {
		return false;
	}
	VulkanRenderer::instance->registerVoxelNodeProxy(node);
	return true;
}

inline void unregisterVoxelNodeProxy(toast::VoxelNode* node) {
	if (VulkanRenderer::instance != nullptr) {
		VulkanRenderer::instance->unregisterVoxelNodeProxy(node);
	}
}

inline void registerLightNodeProxy(toast::Light* node) {
	VulkanRenderer::instance->registerLightNodeProxy(node);
}

inline void unregisterLightNodeProxy(toast::Light* node) {
	VulkanRenderer::instance->unregisterLightNodeProxy(node);
}

inline void registerCameraNodeProxy(toast::Camera* node) {
	if (VulkanRenderer::instance != nullptr) {
		VulkanRenderer::instance->registerCameraNodeProxy(node);
	}
}

inline void unregisterCameraNodeProxy(toast::Camera* node) {
	if (VulkanRenderer::instance != nullptr) {
		VulkanRenderer::instance->unregisterCameraNodeProxy(node);
	}
}

inline void registerReflectionProbeProxy(toast::ReflectionProbe* node) {
	VulkanRenderer::instance->registerReflectionProbeProxy(node);
}

inline void unregisterReflectionProbeProxy(toast::ReflectionProbe* node) {
	VulkanRenderer::instance->unregisterReflectionProbeProxy(node);
}

inline void registerIrradianceVolumeProxy(toast::IrradianceVolume* node) {
	VulkanRenderer::instance->registerIrradianceVolumeProxy(node);
}

inline void unregisterIrradianceVolumeProxy(toast::IrradianceVolume* node) {
	VulkanRenderer::instance->unregisterIrradianceVolumeProxy(node);
}

inline void registerPostProcessVolumeProxy(toast::PostProcessVolume* node) {
	VulkanRenderer::instance->registerPostProcessVolumeProxy(node);
}

inline void unregisterPostProcessVolumeProxy(toast::PostProcessVolume* node) {
	VulkanRenderer::instance->unregisterPostProcessVolumeProxy(node);
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

/// Not thread safe
inline auto renderingFrame() -> const VulkanRenderer::RenderFrame* {
	return VulkanRenderer::instance->renderingFrame();
}

/// DEBUG LINES

void debugDrawSolidSphere(glm::vec3 center, float radius, glm::vec4 color);
void debugDrawShapeBox(const glm::mat4& transform, glm::vec4 color, bool fill);
void debugDrawCapsule(const glm::mat4& transform, float radius, float height, glm::vec4 color, bool fill);

/**
 * @brief Queues a debug line segmentfor the frame currently being built
 * @note Call between beginFrameBuild() and submitFrame()
 */
inline void debugDrawLine(glm::vec3 a, glm::vec3 b, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	auto& frame = VulkanRenderer::instance->beginFrameBuild();
	frame.debug_line_vertices.push_back({a, color});
	frame.debug_line_vertices.push_back({b, color});
}

inline void debugDrawBox(glm::vec3 min, glm::vec3 max, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
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

inline void debugDrawSphere(glm::vec3 center, float radius, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, int segments = 24) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
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

inline void debugDrawAxes(const glm::mat4& transform) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	VulkanRenderer::instance->beginFrameBuild().debug_gizmo_instances.push_back(transform);
}

inline void debugDrawBillboard(
    glm::vec3 world_position, float size, assets::Handle<assets::Texture> texture, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}
) {
	if (!texture.hasValue() || !VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	VulkanRenderer::instance->beginFrameBuild().debug_billboards.push_back(
	    VulkanRenderer::DebugBillboard {
	      .position = world_position,
	      .size = size,
	      .tint = tint,
	      .texture = std::move(texture),
	    }
	);
}

inline void debugDrawMesh(
    const assets::Handle<assets::Mesh>& mesh, const glm::mat4& transform, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}
) {
	if (!mesh.hasValue() || !VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	VulkanRenderer::instance->beginFrameBuild().debug_meshes.push_back(
	    VulkanRenderer::DebugMesh {.model = transform, .tint = tint, .mesh = mesh}
	);
}

void debugDrawMesh(toast::UID mesh, const glm::mat4& transform, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// @note Crashes if the UID is not a texture
void debugDrawBillboard(glm::vec3 world_position, float size, toast::UID texture, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});

inline void debugDrawArrow(glm::vec3 from, glm::vec3 to, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, float head_size = 0.2f) {
	debugDrawLine(from, to, color);

	const glm::vec3 dir = to - from;
	const float len = glm::length(dir);
	if (len < 0.0001f) {
		return;
	}
	const glm::vec3 axis = dir / len;
	const glm::vec3 up = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 side = glm::normalize(glm::cross(up, axis));

	const glm::vec3 back = to - axis * head_size;
	debugDrawLine(to, back + side * head_size * 0.5f, color);
	debugDrawLine(to, back - side * head_size * 0.5f, color);
}

void debugDrawCone(
    glm::vec3 apex, glm::vec3 direction, float length, float half_angle_degrees, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
    int segments = 24
);

void debugDrawFrustum(
    const toast::Camera& camera, float aspect, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f}, float far_override = 0.0f
);

void debugDrawFrustumFromMatrix(const glm::mat4& view_projection, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f});

}    // namespace renderer
