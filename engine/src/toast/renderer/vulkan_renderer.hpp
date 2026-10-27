/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026

#pragma once

#include "compute_pass_base.hpp"
#include "debug_draw.hpp"
#include "depth_convention.hpp"
#include "gi_settings.hpp"
#include "gpu_sync.hpp"
#include "gpu_timer.hpp"
#include "output_target_base.hpp"
#include "post_process_pass_base.hpp"
#include "post_process_settings.hpp"
#include "render_pass_base.hpp"
#include "shader_layout.hpp"
#include "shadow_constants.hpp"
#include "shadow_slots.hpp"
#include "voxel_blas_cache.hpp"
#include "voxel_gpu_storage.hpp"
#include "voxel_scene_patcher.hpp"
#include "voxel_trace_scene.hpp"
#include "vulkan_core.hpp"
#include "vulkan_mesh.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_texture.hpp"

#include <algorithm>
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
#include <span>
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
class FogVolume;
class INodeOwner;
class Node;
class Node3D;
}

namespace renderer {

class MaterialPass;
class ClusterLightingPass;
class ShadowPass;
class EnvironmentPass;
class ReflectionProbePass;
class DepthPrepass;
class SelectionOutline;
class SkinningPass;
class VoxelPass;
class DebrisPass;
class GiPass;
class TracedSpecularPass;
class TaaPass;
class RayTracingScene;
class SkinnedBlasPool;

class VulkanRenderer {
public:
	[[nodiscard]]
	static auto selectDepthFormat(const VulkanCore& core) -> vk::Format;

	static constexpr uint32_t k_frames_in_flight = 3;

	/// One drawing and one building
	static constexpr uint8_t k_render_frames = 2;
	/// waitForOutputImage rounds of about 8 ms before a snapshot is dropped
	static constexpr uint32_t k_output_acquire_attempts = 12;

	static constexpr uint32_t k_max_fog_volumes = 32;

	/// Mirrors volumetrics.slang FogVolume
	struct FogVolumeGpu {
		glm::mat4 world_to_local {1.0f};
		/// xyz half extents w edge fade in meters
		glm::vec4 extents_fade {0.0f};
		/// rgb albedo a extinction per meter
		glm::vec4 albedo_density {0.0f};
		/// rgb radiance per meter at density 1 a noise strength
		glm::vec4 emission_noise {0.0f};
		/// xyz wind in meters per second w noise scale in meters
		glm::vec4 wind_scale {0.0f};
		/// x anisotropy y ambient scale
		glm::vec4 phase {0.0f};
		/// xyz box centre in world space
		glm::vec4 centre {0.0f};
	};

	struct FrameContext {
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::Semaphore image_available = nullptr;
		vk::raii::Fence in_flight = nullptr;
		uint32_t last_image_index = 0;
		bool has_submitted = false;
		bool was_probe_capture = false;

		std::vector<vma::raii::Buffer> blas_scratch;

		/// Released once this frame fence was waited on again
		std::vector<std::shared_ptr<void>> retired;
	};

	static constexpr uint32_t k_max_directional_lights = 4;

	static constexpr uint32_t k_max_joint_matrices = 4096;

	static constexpr uint32_t k_max_instances = 16384;

	static constexpr uint32_t k_instance_no_motion = 1u << 0;

	/// Mirrors mesh.slang depth_prepass.slang and shadow_depth.slang InstanceData
	struct InstanceData {
		glm::mat4 model {1.0f};
		glm::mat4 previous_model {1.0f};
		uint32_t joint_offset = 0;
		uint32_t flags = 0;
		std::array<uint32_t, 2> pad0 {0, 0};
	};

	static_assert(sizeof(InstanceData) == 144, "InstanceData stride must match mesh.slang's std430 layout");

	struct DirectionalLightData {
		/// w angular radius in radians when voxel shadows are marched and 0 for the shadow map
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

		/// view_projection with the TAA jitter for world raster only
		glm::mat4 jittered_view_projection {1.0f};

		/// Unjittered view_projection of the last presented frame
		glm::mat4 previous_view_projection {1.0f};

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

		/// xy blue noise texel offset walking R2 per frame z frame index
		glm::vec4 noise_params {0.0f};

		/// x height fog extinction per meter at the base height y base height z height falloff w fog volumes
		glm::vec4 fog_medium {0.0f};
		/// x height fog distance y fog volume distance z marched sun reach in meters
		glm::vec4 fog_reach {0.0f};
	};

	static_assert(
	    offsetof(FrameUBO, jittered_view_projection) == 208,
	    "FrameUBO::jittered_view_projection must match lighting.slang and depth_prepass.slang"
	);
	static_assert(
	    offsetof(FrameUBO, irradiance_volumes) == 1264, "FrameUBO::irradiance_volumes must match lighting.slang's std140 layout"
	);
	static_assert(offsetof(FrameUBO, noise_params) == 1536, "FrameUBO::noise_params must match lighting.slang's std140 layout");
	static_assert(
	    offsetof(FrameUBO, traced_shadow_params) == 1520, "FrameUBO::traced_shadow_params must match lighting.slang's std140 layout"
	);

	/// Mirrors GpuLight in lighting.slang and cluster_lighting.slang
	struct GpuLight {
		glm::vec4 world_pos_range;
		glm::vec4 view_pos_type;     // w type 0 point 1 spot
		glm::vec4 view_direction;    // xyz view space spot axis
		glm::vec4 color_intensity;
		glm::vec4 direction_pad;     // w shadow layer or -1
		glm::vec4 cone_angles;       // x cos outer y cos inner z shadow near w shadow far

		// x fraction of the layer rendered y point light face scale inside the guard band z shadow strength
		glm::vec4 shadow_atlas {1.0f, 1.0f, 0.0f, 0.0f};

		glm::mat4 shadow_view_projection {1.0f};

		/// x toast::LightShape y radius or rect half width z capsule half length or rect half height w toast::LightShadowMode
		/// The shape faces or runs along direction_pad.xyz
		glm::vec4 shape {0.0f};

		/// xyz rect width axis
		glm::vec4 shape_tangent {0.0f};
	};

	static_assert(sizeof(GpuLight) == 208, "GpuLight must match the stride in lighting.slang and cluster_lighting.slang");

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

		uint64_t node_rid = 0;

		/// Selected in the editor hierarchy
		bool outlined = false;
	};

	struct VoxelVolumeProxy {
		uint32_t record_index = 0;

		glm::uvec3 brick_dims {0};

		glm::mat4 model {1.0f};
		glm::mat4 inverse_model {1.0f};

		glm::mat4 previous_model {1.0f};

		/// False the first frame this node_uid is drawn so the shader reports motion as not written instead of zero
		bool has_previous = false;

		glm::vec3 bounds_center {0.0f};
		float bounds_radius = 0.0f;

		float view_distance = 0.0f;

		bool camera_inside = false;

		bool mirrored = false;

		bool visible = true;

		uint64_t node_rid = 0;
		uint32_t highlight_record = 0;

		/// Selected in the editor hierarchy
		bool outlined = false;
	};

	struct UIWorldPanelProxy {
		vk::ImageView view = nullptr;
		glm::mat4 model = glm::mat4(1.0f);
		bool always_on_top = false;
	};

	struct SizeHandleDraw {
		glm::vec3 world_position {0.0f};
		toast::GizmoHandle handle = toast::GizmoHandle::none;
	};

	static constexpr size_t k_max_size_handles = 6;

	struct TransformGizmoDraw {
		bool visible = false;
		toast::GizmoTool tool = toast::GizmoTool::select;
		glm::mat4 model {1.0f};
		toast::GizmoHandle hover = toast::GizmoHandle::none;
		toast::GizmoHandle active = toast::GizmoHandle::none;
		float drag_scale_factor = 1.0f;

		std::array<SizeHandleDraw, k_max_size_handles> size_handles {};
		uint32_t size_handle_count = 0;
		float size_handle_scale = 1.0f;
	};

	struct GizmoState {
		bool visible = false;
		toast::GizmoTool tool = toast::GizmoTool::select;
		glm::vec3 origin {0.0f};
		glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
		toast::GizmoHandle hover = toast::GizmoHandle::none;
		toast::GizmoHandle active = toast::GizmoHandle::none;
		float drag_scale_factor = 1.0f;

		std::array<SizeHandleDraw, k_max_size_handles> size_handles {};
		uint32_t size_handle_count = 0;
		float size_handle_scale = 1.0f;
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

	struct LightStats {
		uint32_t submitted = 0;
		uint32_t offscreen = 0;
		uint32_t truncated = 0;
		uint32_t spot_shadows = 0;
		uint32_t point_shadows = 0;
		/// Lights within their shadow distance left without a shadow because every slot was taken
		uint32_t spot_denied = 0;
		uint32_t point_denied = 0;
		uint64_t shadow_displaced = 0;
	};

	struct RenderFrame {
		FrameUBO frame_data;

		std::vector<MeshInstanceProxy> mesh_instances;

		/// Sorted front to back
		std::vector<VoxelVolumeProxy> voxel_instances;

		std::shared_ptr<const VoxelGpuStorage> voxel_storage;

		/// Camera near plane distance so voxel cells it clips draw with the camera inside variant
		float near_plane = 0.0f;

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

		float cluster_near = 0.1f;
		float cluster_far = 5000.0f;

		LightStats light_stats;

		ImGuiInputSnapshot imgui_input;

		uint32_t render_mode = 0;

		uint64_t sequence = 0;

		/// Jittered and resolved by TaaPass which then skips FxaaPass
		bool taa_active = false;

		/// History is stale since the camera switched or TAA just turned on
		bool taa_reset = false;

		PostProcessSettings post_process;

		/// Nearest first and at most k_max_fog_volumes
		std::vector<FogVolumeGpu> fog_volumes;

		int32_t probe_capture_index = -1;
		uint32_t probe_capture_face = 0;

		int32_t irradiance_capture_index = -1;
		uint32_t irradiance_capture_face = 0;

		uint32_t capture_extent = 0;

		std::vector<debug::Vertex> debug_line_vertices;    // pairs form line segments
		std::vector<debug::Vertex> debug_triangle_vertices;
		std::vector<glm::mat4> debug_gizmo_instances;
		std::vector<debug::Billboard> debug_billboards;
		std::vector<debug::Mesh> debug_meshes;

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
	auto getCurrentFrameBuild() noexcept -> RenderFrame& {
		return m_render_frames[m_write_index];
	}

	void submitFrame() noexcept;

	/// For the next frame when called before frame construction begins
	void queueDebugLine(glm::vec3 a, glm::vec3 b, glm::vec4 color);

	/// Tick thread before input is polled
	void waitForFrameSlot() noexcept;

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

	void registerFogVolumeProxy(toast::FogVolume* node);

	void unregisterFogVolumeProxy(toast::FogVolume* node);

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

	void setDebugWindowsVisible(bool visible) noexcept { m_debug_windows_visible.store(visible, std::memory_order_relaxed); }

	[[nodiscard]]
	auto debugWindowsVisible() const noexcept -> bool {
		return m_debug_windows_visible.load(std::memory_order_relaxed);
	}

	void setDebugOverlayInGame(bool visible) noexcept { m_debug_overlay_in_game.store(visible, std::memory_order_relaxed); }

	[[nodiscard]]
	auto debugOverlayInGame() const noexcept -> bool {
		return m_debug_overlay_in_game.load(std::memory_order_relaxed);
	}

	/// Main thread only, once per tick
	void setGameView(bool game_view);

	/// ImGui draws nothing while a game camera is live unless shown in game
	[[nodiscard]]
	auto debugOverlayHidden() const noexcept -> bool {
		return m_game_view.load(std::memory_order_relaxed) && !debugOverlayInGame();
	}

	[[nodiscard]]
	auto debugOverlayOwnsCursor() const noexcept -> bool {
		return m_debug_overlay_owns_cursor.load(std::memory_order_acquire);
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

	[[nodiscard]]
	auto getSceneMotionView() const noexcept -> vk::ImageView {
		return m_scene_motion.view.has_value() ? **m_scene_motion.view : vk::ImageView {};
	}

	[[nodiscard]]
	auto getVoxelAlbedoView() const noexcept -> vk::ImageView {
		return m_voxel_albedo.view.has_value() ? **m_voxel_albedo.view : vk::ImageView {};
	}

	[[nodiscard]]
	auto getVoxelMaterialView() const noexcept -> vk::ImageView {
		return m_voxel_material.view.has_value() ? **m_voxel_material.view : vk::ImageView {};
	}

	/// Holds flat face normals not the rounded scene normal ones
	[[nodiscard]]
	auto getVoxelFaceNormalView() const noexcept -> vk::ImageView {
		return m_voxel_face_normal.view.has_value() ? **m_voxel_face_normal.view : vk::ImageView {};
	}

	/// Marched and sampled diffuse over albedo before denoising
	[[nodiscard]]
	auto getVoxelNoisyDiffuseView() const noexcept -> vk::ImageView {
		return m_voxel_noisy_diffuse.view.has_value() ? **m_voxel_noisy_diffuse.view : vk::ImageView {};
	}

	[[nodiscard]]
	auto getVoxelNoisySpecularView() const noexcept -> vk::ImageView {
		return m_voxel_noisy_specular.view.has_value() ? **m_voxel_noisy_specular.view : vk::ImageView {};
	}

	/// Last frame's reservoirs that this frame's voxel lighting reads
	[[nodiscard]]
	auto getVoxelPreviousReservoirView() const noexcept -> vk::ImageView {
		const auto& target = m_voxel_reservoirs[1 - m_voxel_reservoir_parity];
		return target.view.has_value() ? **target.view : vk::ImageView {};
	}

	/// This frame's reservoirs before the spatial pass merges them
	[[nodiscard]]
	auto getVoxelCandidateReservoirView() const noexcept -> vk::ImageView {
		return m_voxel_candidate_reservoirs.view.has_value() ? **m_voxel_candidate_reservoirs.view : vk::ImageView {};
	}

	/// The previous reservoirs were written by the frame right before this one
	[[nodiscard]]
	auto voxelReservoirHistory() const noexcept -> bool {
		return m_voxel_reservoir_history;
	}

	/// Edge rounding radius in voxels where 0 keeps the flat face normals
	void setVoxelCellBoxes(bool enabled) noexcept { m_voxel_cell_boxes.store(enabled, std::memory_order_relaxed); }

	void setVoxelOcclusion(bool enabled) noexcept { m_voxel_occlusion.store(enabled, std::memory_order_relaxed); }

	void setVoxelMeshShaders(bool enabled) noexcept { m_voxel_mesh_shaders.store(enabled, std::memory_order_relaxed); }

	/// Share of listed cells occlusion must hide to stay on where 0 never idles it
	void setVoxelOcclusionMinHidden(float share) noexcept { m_voxel_occlusion_min_hidden.store(share, std::memory_order_relaxed); }

	/// Frames occlusion stays off once it hid too little before it measures again
	void setVoxelOcclusionIdleFrames(uint32_t frames) noexcept {
		m_voxel_occlusion_idle_frames.store(frames, std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelOcclusionMinHidden() const noexcept -> float {
		return m_voxel_occlusion_min_hidden.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelOcclusionIdleFrames() const noexcept -> uint32_t {
		return m_voxel_occlusion_idle_frames.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelMeshShaders() const noexcept -> bool {
		return m_voxel_mesh_shaders.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelOcclusion() const noexcept -> bool {
		return m_voxel_occlusion.load(std::memory_order_relaxed);
	}

	/// Smallest on screen voxel size in pixels that is rasterized instead of marched and 0 marches everything
	void setVoxelRasterPixels(float pixels) noexcept {
		m_voxel_raster_pixels.store(std::max(pixels, 0.0f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelRasterPixels() const noexcept -> float {
		return m_voxel_raster_pixels.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelCellBoxes() const noexcept -> bool {
		return m_voxel_cell_boxes.load(std::memory_order_relaxed);
	}

	/// Candidates the voxel lighting resamples among the froxel's sampled lights
	void setVoxelLightCandidates(uint32_t count) noexcept { m_voxel_light_candidates.store(count, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelLightCandidates() const noexcept -> uint32_t {
		return m_voxel_light_candidates.load(std::memory_order_relaxed);
	}

	/// Traced ambient occlusion on voxel pixels in place of SSAO
	void setVoxelAmbientRays(bool enabled) noexcept { m_voxel_ambient_rays.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelAmbientRays() const noexcept -> bool {
		return m_voxel_ambient_rays.load(std::memory_order_relaxed);
	}

	void setVoxelAmbientRayQuery(bool enabled) noexcept { m_voxel_ambient_ray_query.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelAmbientRayQuery() const noexcept -> bool {
		return m_voxel_ambient_ray_query.load(std::memory_order_relaxed);
	}

	void setVoxelAmbientReach(float meters) noexcept {
		m_voxel_ambient_reach.store(std::max(meters, 0.0f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelAmbientReach() const noexcept -> float {
		return m_voxel_ambient_reach.load(std::memory_order_relaxed);
	}

	void setVolumetrics(bool enabled) noexcept { m_volumetrics.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto volumetrics() const noexcept -> bool {
		return m_volumetrics.load(std::memory_order_relaxed);
	}

	void setVolumetricSteps(uint32_t steps) noexcept { m_volumetric_steps.store(std::max(steps, 1u), std::memory_order_relaxed); }

	static constexpr uint32_t k_volumetric_quality_custom = 4;

	void setVolumetricQuality(uint32_t level) noexcept {
		m_volumetric_quality.store(std::min(level, k_volumetric_quality_custom), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto volumetricQuality() const noexcept -> uint32_t {
		return m_volumetric_quality.load(std::memory_order_relaxed);
	}

	void setVolumetricLights(uint32_t count) noexcept { m_volumetric_lights.store(count, std::memory_order_relaxed); }

	[[nodiscard]]
	auto volumetricLights() const noexcept -> uint32_t {
		return m_volumetric_lights.load(std::memory_order_relaxed);
	}

	void setVolumetricLightShadows(bool enabled) noexcept { m_volumetric_light_shadows.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto volumetricLightShadows() const noexcept -> bool {
		return m_volumetric_light_shadows.load(std::memory_order_relaxed);
	}

	void setVolumetricQuarterResolution(bool quarter) noexcept { m_volumetric_quarter.store(quarter, std::memory_order_relaxed); }

	/// Meters fog volumes are marched to while height fog keeps its own Fog Distance
	void setVolumetricDistance(float meters) noexcept {
		m_volumetric_distance.store(std::max(meters, 1.0f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto volumetricDistance() const noexcept -> float {
		return m_volumetric_distance.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto volumetricQuarterResolution() const noexcept -> bool {
		return m_volumetric_quarter.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto volumetricSteps() const noexcept -> uint32_t {
		return m_volumetric_steps.load(std::memory_order_relaxed);
	}

	void setVoxelTracedSpecular(bool enabled) noexcept { m_voxel_traced_specular.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelTracedSpecular() const noexcept -> bool {
		return m_voxel_traced_specular.load(std::memory_order_relaxed);
	}

	/// Rougher voxels keep the environment specular
	void setVoxelSpecularRoughness(float roughness) noexcept {
		m_voxel_specular_roughness.store(std::clamp(roughness, 0.0f, 1.0f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelSpecularRoughness() const noexcept -> float {
		return m_voxel_specular_roughness.load(std::memory_order_relaxed);
	}

	void setVoxelSpecularReach(float meters) noexcept {
		m_voxel_specular_reach.store(std::max(meters, 0.1f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelSpecularReach() const noexcept -> float {
		return m_voxel_specular_reach.load(std::memory_order_relaxed);
	}

	/// 1 is Snell and 0 turns refraction off
	void setVoxelGlassRefraction(float strength) noexcept {
		m_voxel_glass_refraction.store(std::max(strength, 0.0f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelGlassRefraction() const noexcept -> float {
		return m_voxel_glass_refraction_enabled.load(std::memory_order_relaxed)
		           ? m_voxel_glass_refraction.load(std::memory_order_relaxed)
							 : 0.0f;
	}

	/// Off skips the backdrop copy and the volume march of every glass pixel
	void setVoxelGlassRefractionEnabled(bool enabled) noexcept {
		m_voxel_glass_refraction_enabled.store(enabled, std::memory_order_relaxed);
	}

	void setVoxelGlassRefractionDistance(float meters) noexcept {
		m_voxel_glass_refraction_distance.store(std::max(meters, 0.0f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelGlassRefractionDistance() const noexcept -> float {
		return m_voxel_glass_refraction_distance.load(std::memory_order_relaxed);
	}

	void setVoxelGlassRefractionVoxels(uint32_t voxels) noexcept {
		m_voxel_glass_refraction_voxels.store(std::max(voxels, 1u), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelGlassRefractionVoxels() const noexcept -> uint32_t {
		return m_voxel_glass_refraction_voxels.load(std::memory_order_relaxed);
	}

	void setVoxelGlassLitSurfaces(uint32_t surfaces) noexcept {
		m_voxel_glass_lit_surfaces.store(surfaces, std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelGlassLitSurfaces() const noexcept -> uint32_t {
		return m_voxel_glass_lit_surfaces.load(std::memory_order_relaxed);
	}

	void setVoxelGlassShadows(bool enabled) noexcept { m_voxel_glass_shadows.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelGlassShadows() const noexcept -> bool {
		return m_voxel_glass_shadows.load(std::memory_order_relaxed);
	}

	void setVoxelGlassShadowReach(uint32_t meters) noexcept {
		m_voxel_glass_shadow_reach.store(std::max(meters, 1u), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelGlassShadowReach() const noexcept -> uint32_t {
		return m_voxel_glass_shadow_reach.load(std::memory_order_relaxed);
	}

	/// Scene colour and depth copied after the sky for glass to refract
	[[nodiscard]]
	auto getGlassBackdropView() const noexcept -> vk::ImageView {
		return m_glass_backdrop.view ? **m_glass_backdrop.view : vk::ImageView {};
	}

	[[nodiscard]]
	auto getGlassBackdropDepthView() const noexcept -> vk::ImageView {
		return m_glass_backdrop_depth.view ? **m_glass_backdrop_depth.view : vk::ImageView {};
	}

	/// Meters a marched sun ray travels before it counts as lit
	void setVoxelSunReach(uint32_t meters) noexcept { m_voxel_sun_reach.store(meters, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelSunReach() const noexcept -> uint32_t {
		return m_voxel_sun_reach.load(std::memory_order_relaxed);
	}

	void setVoxelTraceRayQuery(bool enabled) noexcept { m_voxel_trace_ray_query.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelTraceRayQuery() const noexcept -> bool {
		return m_voxel_trace_ray_query.load(std::memory_order_relaxed);
	}

	/// Off keeps every volume on the unit box BLAS so the two can be compared
	void setVoxelTraceBricks(bool enabled) noexcept { m_voxel_trace_bricks.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelTraceBricks() const noexcept -> bool {
		return m_voxel_trace_bricks.load(std::memory_order_relaxed);
	}

	void setVoxelBlasBudget(uint32_t bricks) noexcept { m_voxel_blas_budget.store(bricks, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelBlasBudget() const noexcept -> uint32_t {
		return m_voxel_blas_budget.load(std::memory_order_relaxed);
	}

	/// Meters per cell of the grid marched rays walk without ray queries
	void setVoxelTraceCellSize(float meters) noexcept {
		m_voxel_trace_cell_size.store(std::max(meters, 0.25f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelTraceCellSize() const noexcept -> float {
		return m_voxel_trace_cell_size.load(std::memory_order_relaxed);
	}

	/// Nearest drawn voxel instances a marched shadow ray tests
	void setVoxelShadowInstances(uint32_t count) noexcept { m_voxel_shadow_instances.store(count, std::memory_order_relaxed); }

	[[nodiscard]]
	auto voxelShadowInstances() const noexcept -> uint32_t {
		return m_voxel_shadow_instances.load(std::memory_order_relaxed);
	}

	void setVoxelNormalRounding(float voxels) noexcept {
		m_voxel_normal_rounding.store(std::max(voxels, 0.0f), std::memory_order_relaxed);
	}

	[[nodiscard]]
	auto voxelNormalRounding() const noexcept -> float {
		return m_voxel_normal_rounding.load(std::memory_order_relaxed);
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
	/// renderer::blue_noise::generatePacked texels bound to lighting.slang gBlueNoise
	[[nodiscard]]
	auto getBlueNoiseBuffer() const -> vk::Buffer {
		return m_blue_noise_buffer.has_value() ? **m_blue_noise_buffer : vk::Buffer {};
	}

	/// fog_media.slang gFogVolumes
	auto getFogVolumeBuffer(uint32_t frame_index) const -> vk::Buffer {
		return frame_index < m_fog_volume_res.size() && m_fog_volume_res[frame_index].gpu_buffer.has_value()
		           ? **m_fog_volume_res[frame_index].gpu_buffer
							 : vk::Buffer {};
	}

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

	/// Still registered with addRenderPass so it lists and toggles like any pass
	void setVoxelPass(VoxelPass* pass) noexcept { m_voxel_pass = pass; }

	void setDebrisPass(DebrisPass* pass) noexcept { m_debris_pass = pass; }

	void setGiPass(GiPass* pass) noexcept { m_gi_pass = pass; }

	/// Still registered with addPostProcessPass so it runs in the post chain
	void setTracedSpecularPass(TracedSpecularPass* pass) noexcept { m_traced_specular_pass = pass; }

	void setTaaPass(TaaPass* pass) noexcept { m_taa_pass = pass; }

	/// Render thread before TaaPass records and null on a camera cut or without TAA
	[[nodiscard]]
	auto previousResolvedColour() const noexcept -> vk::ImageView;

	/// Render thread whether voxel lighting left the environment specular of traced pixels to the traced specular pass
	[[nodiscard]]
	auto tracedSpecularFrame() const noexcept -> bool {
		return m_traced_specular_frame;
	}

	[[nodiscard]]
	auto giPass() const noexcept -> GiPass* {
		return m_gi_pass;
	}

	[[nodiscard]]
	auto giSettings() noexcept -> GiSettings& {
		return m_gi_settings;
	}

	[[nodiscard]]
	auto giSettings() const noexcept -> const GiSettings& {
		return m_gi_settings;
	}

	[[nodiscard]]
	auto voxelCollisionWorld() const -> std::shared_ptr<const VoxelCollisionWorld>;

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
	auto voxelTraceScene() const noexcept -> VoxelTraceScene* {
		return m_voxel_trace_scene.get();
	}

	/// Null without ray tracing
	[[nodiscard]]
	auto voxelBlasCache() const noexcept -> const VoxelBlasCache* {
		return m_voxel_blas_cache.get();
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

	/// Node rids whose meshes and voxels get the selection outline
	void setOutlinedNodes(std::vector<uint64_t> rids) noexcept {
		std::ranges::sort(rids);
		m_outlined_rids = std::move(rids);
	}

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

	struct UploadStats {
		uint32_t slots_in_flight = 0;
		uint32_t slot_count = 0;
		size_t waiting_jobs = 0;
		size_t batches_in_flight = 0;
		vk::DeviceSize host_bytes = 0;
		vk::DeviceSize host_budget = 0;
	};

	/// @note Render thread only
	[[nodiscard]]
	auto uploadStats() -> UploadStats;

	/// Signalled by every transfer submit so a submit needing an upload waits on its value
	[[nodiscard]]
	auto uploadTimeline() const noexcept -> vk::Semaphore {
		return *m_upload_timeline;
	}

	/// Render thread only
	[[nodiscard]]
	auto uploadTimelineValue() const noexcept -> uint64_t {
		return m_upload_timeline_value;
	}

	/// Render thread and frees @p object once the frame being recorded finished on the GPU
	void retire(std::shared_ptr<void> object);

	/// @note Render thread only
	[[nodiscard]]
	auto gpuTimer() const noexcept -> const GpuTimer* {
		return m_gpu_timer.get();
	}

	/// @note Render thread only
	[[nodiscard]]
	auto gpuTimer() noexcept -> GpuTimer* {
		return m_gpu_timer.get();
	}

	void setTracedShadowsEnabled(bool enabled) noexcept { m_traced_shadows_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto tracedShadowsEnabled() const noexcept -> bool {
		return m_traced_shadows_enabled.load(std::memory_order_relaxed);
	}

	void setTaaEnabled(bool enabled) noexcept { m_taa_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto taaEnabled() const noexcept -> bool {
		return m_taa_enabled.load(std::memory_order_relaxed);
	}

	void setTaaHistoryWeight(float weight) noexcept { m_taa_history_weight.store(weight, std::memory_order_relaxed); }

	[[nodiscard]]
	auto taaHistoryWeight() const noexcept -> float {
		return m_taa_history_weight.load(std::memory_order_relaxed);
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

	/**
	 * Tints the voxels of node that are solid in highlight
	 * @note nullptr clears it
	 */
	void setVoxelHighlight(const toast::VoxelNode* node, const voxel::Volume* highlight, uint64_t part_key = 0) {
		std::scoped_lock lock(m_voxel_proxy_mutex);
		if (highlight == nullptr) {
			m_voxel_highlights.erase(VoxelHighlightKey {node, part_key});
		} else {
			m_voxel_highlights[VoxelHighlightKey {node, part_key}] = highlight;
		}
	}

	void clearVoxelHighlights(const toast::VoxelNode* node) {
		std::scoped_lock lock(m_voxel_proxy_mutex);
		std::erase_if(m_voxel_highlights, [node](const auto& entry) { return entry.first.node == node; });
	}

	void setSecondaryOwner(const toast::INodeOwner* owner, const glm::mat4& transform, const toast::Node* hidden) {
		m_secondary_owner = owner;
		m_secondary_transform = transform;
		m_secondary_hidden = hidden;
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
	/// False when nothing was rendered
	auto drawFrame(RenderFrame& frame_data) -> bool;
	void waitForOutputImage();

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

	void acquireFrameSlot() noexcept;

	/// Tick thread only
	enum class FrameSlot : uint8_t {
		none,
		held,
		missed
	};
	FrameSlot m_frame_slot = FrameSlot::none;
	std::chrono::steady_clock::time_point m_tick_deadline;

	struct DepthResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		sync::ImageState state;
	};

	struct SceneColorResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
		sync::ImageState state;
	};

	void createGraphicsCommandPool();
	void createTransferCommandPool();
	void createFrameContexts();

	void createPerImageSync();
	void createDepthResources();
	void createSceneColorResources();

	/// @returns true when this frame had voxels to draw
	auto recordVoxelScopes(FrameContext& frame, const vk::Viewport& viewport, const vk::Rect2D& scissor, bool depth_loaded) -> bool;

	void publishCompletedFrames();

	void updateVoxelTraceScene();
	void recordAccelerationStructureBuilds(FrameContext& frame);

	void createPresentResources();
	void createDescriptorPool();
	void createDefaultTexture();

	void createFailsafeTextures();
	void createDefaultShadowMap();
	void createDefaultCubemap();

	void ensureMaterialPasses(RenderFrame& frame_data);
	/// After the pass updates so the prepass never writes depth the color pass leaves empty
	void hideInstancesWithoutPipeline(RenderFrame& frame_data);

	void recordFrame(FrameContext& frame, uint32_t image_index) noexcept;

	/// @note Expects an open rendering scope with m_pass_mutex held
	/// Opaque meshes then the sky then blended meshes and voxel glass far to near
	/// @p capture_backdrop ends the world scope copies the scene for refracting glass and resumes it
	void recordMeshScene(
	    vk::CommandBuffer cmd, uint32_t image_index, bool voxel_glass, const std::function<void()>& capture_backdrop
	);

	struct PunctualShadowCandidate {
		size_t light_index = 0;
		float distance_squared = 0.0f;
		float resolution_scale = 1.0f;
		/// The light shadow distance where 0 is no limit
		float shadow_distance = 0.0f;
		bool visible = true;
		uint64_t key = 0;
		float importance = 0.0f;
	};

	void fitShadowViews(
	    RenderFrame& frame, float aspect, int32_t shadow_caster_index, const glm::vec3& shadow_caster_direction,
	    const std::vector<PunctualShadowCandidate>& spot_candidates, const std::vector<PunctualShadowCandidate>& point_candidates
	);

	/// Fills m_tick_slot_of with the slot of each candidate or -1
	void assignShadowSlots(const std::vector<PunctualShadowCandidate>& candidates, std::span<shadow_slots::Slot> slots);

	void releaseShadowSlots();

	/// Drops unseen lights without a shadow slot then ranks and caps what is left
	void finalizeLights(RenderFrame& frame);

	struct UploadSlot {
		vk::raii::CommandBuffer command_buffer = nullptr;
		/// m_upload_timeline reaches it once the batch completed
		uint64_t timeline_value = 0;
		bool in_flight = false;
	};

	static constexpr uint32_t k_upload_slots = 4;

	struct BatchedUploadGroup {
		std::vector<std::unique_ptr<PendingResourceUpload>> jobs;
		uint32_t slot = 0;
	};

	vk::raii::CommandPool m_command_pool = nullptr;
	vk::raii::CommandPool m_transfer_command_pool = nullptr;
	vk::raii::DescriptorPool m_descriptor_pool = nullptr;

	std::vector<UploadSlot> m_upload_slots;
	uint32_t m_next_upload_slot = 0;

	vk::raii::Semaphore m_upload_timeline = nullptr;
	uint64_t m_upload_timeline_value = 0;
	/// Newest value whose acquires are recorded and newest a graphics submit waited on
	uint64_t m_upload_acquired_value = 0;
	uint64_t m_upload_waited_value = 0;

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

	VoxelPass* m_voxel_pass = nullptr;

	DebrisPass* m_debris_pass = nullptr;
	GiPass* m_gi_pass = nullptr;
	GiSettings m_gi_settings;
	TracedSpecularPass* m_traced_specular_pass = nullptr;
	TaaPass* m_taa_pass = nullptr;
	bool m_traced_specular_frame = false;

	std::atomic_bool m_probe_bake_requested {false};
	std::atomic<uint32_t> m_probe_stale_count {0};
	/// probe = cursor / 6 and face = cursor % 6 or -1 when idle
	int32_t m_probe_bake_cursor = -1;
	bool m_probe_auto_baked = false;
	vk::Format m_depth_format = vk::Format::eUndefined;
	DepthResources m_depth_resources;
	DepthResources m_glass_backdrop_depth;

	static constexpr vk::Format m_scene_color_format = vk::Format::eR16G16B16A16Sfloat;
	SceneColorResources m_scene_color;
	SceneColorResources m_glass_backdrop;

	static constexpr vk::Format m_scene_normal_format = renderer::k_scene_normal_format;
	SceneColorResources m_scene_normal;

	static constexpr vk::Format m_scene_indirect_format = renderer::k_scene_indirect_format;
	SceneColorResources m_scene_indirect;

	static constexpr vk::Format m_scene_motion_format = renderer::k_scene_motion_format;
	SceneColorResources m_scene_motion;

	SceneColorResources m_voxel_albedo;
	SceneColorResources m_voxel_material;
	SceneColorResources m_voxel_face_normal;
	SceneColorResources m_voxel_noisy_diffuse;
	SceneColorResources m_voxel_noisy_specular;
	/// The voxel lighting writes the parity one and reads the other
	std::array<SceneColorResources, 2> m_voxel_reservoirs;
	/// The lighting scope's reservoirs when the spatial pass then writes m_voxel_reservoirs
	SceneColorResources m_voxel_candidate_reservoirs;
	uint32_t m_voxel_reservoir_parity = 0;
	bool m_voxel_reservoir_written = false;
	bool m_voxel_reservoir_history = false;
	uint64_t m_voxel_reservoir_sequence = 0;

	std::atomic<float> m_voxel_normal_rounding {0.3f};
	std::atomic<uint32_t> m_voxel_light_candidates {8};
	std::atomic<uint32_t> m_voxel_shadow_instances {256};
	std::atomic_bool m_voxel_trace_ray_query {true};
	std::atomic_bool m_voxel_trace_bricks {true};
	std::atomic<uint32_t> m_voxel_blas_budget {262144};
	std::atomic<float> m_voxel_trace_cell_size {4.0f};
	std::atomic<uint32_t> m_voxel_sun_reach {200};
	std::atomic<bool> m_voxel_ambient_rays {true};
	std::atomic<bool> m_voxel_ambient_ray_query {true};
	std::atomic<float> m_voxel_ambient_reach {2.4f};
	std::atomic_bool m_voxel_traced_specular {true};
	std::atomic_bool m_volumetrics {true};
	std::atomic<uint32_t> m_volumetric_steps {24};
	std::atomic<uint32_t> m_volumetric_quality {2};
	std::atomic<uint32_t> m_volumetric_lights {8};
	std::atomic_bool m_volumetric_light_shadows {true};
	std::atomic_bool m_volumetric_quarter {false};
	std::atomic<float> m_volumetric_distance {100.0f};
	std::atomic<float> m_voxel_specular_roughness {0.6f};
	std::atomic<float> m_voxel_specular_reach {32.0f};
	std::atomic<float> m_voxel_glass_refraction {1.0f};
	std::atomic_bool m_voxel_glass_refraction_enabled {true};
	std::atomic<float> m_voxel_glass_refraction_distance {60.0f};
	std::atomic<uint32_t> m_voxel_glass_refraction_voxels {32};
	std::atomic<uint32_t> m_voxel_glass_lit_surfaces {4};
	std::atomic_bool m_voxel_glass_shadows {true};
	std::atomic<uint32_t> m_voxel_glass_shadow_reach {50};
	std::atomic_bool m_voxel_cell_boxes {true};
	std::atomic_bool m_voxel_occlusion {true};
	std::atomic_bool m_voxel_mesh_shaders {true};
	std::atomic<float> m_voxel_occlusion_min_hidden {0.05f};
	std::atomic<uint32_t> m_voxel_occlusion_idle_frames {120};
	std::atomic<float> m_voxel_raster_pixels {4.0f};

	ShaderLayout m_present_layout;
	VulkanPipeline m_present_pipeline;
	vk::raii::Sampler m_present_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_present_sets;
	std::vector<vk::ImageView> m_present_bound_views;

	TracyVkCtx m_tracy_vk_ctx = nullptr;
	std::unique_ptr<GpuTimer> m_gpu_timer;

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_render_finished_per_image;
	std::vector<vk::Fence> m_images_in_flight;
	std::vector<sync::Usage> m_output_image_usage;

	/// Acquire halves of finished uploads flushed at the start of the next frame
	sync::BarrierBatch m_pending_acquires;
	uint32_t m_current_frame = 0;

	toast::Camera* m_camera = nullptr;

	GizmoState m_gizmo_state;

	/// Sorted
	std::vector<uint64_t> m_outlined_rids;

	[[nodiscard]]
	auto isOutlined(uint64_t node_rid) const -> bool {
		return std::ranges::binary_search(m_outlined_rids, node_rid);
	}

	[[nodiscard]]
	auto blendPostProcessVolumes(const glm::vec3& camera_position) -> PostProcessSettings;

	void gatherFogVolumes(RenderFrame& frame, const glm::vec3& camera_position);

	PostProcessSettings m_post_process_settings;

	std::atomic_bool m_traced_shadows_enabled {false};
	/// Logs once each time the fallback starts
	bool m_traced_shadows_voxel_fallback_logged = false;

	std::atomic_bool m_taa_enabled {true};
	std::atomic<float> m_taa_history_weight {0.9f};

	/// Game thread only
	struct TaaHistory {
		glm::mat4 previous_view_projection {1.0f};
		const toast::Camera* camera = nullptr;
		uint64_t jitter_index = 0;
		bool has_previous = false;
		bool was_active = false;
	};

	TaaHistory m_taa_history;

	std::atomic_bool m_debug_draw_enabled {false};
	std::mutex m_debug_line_mutex;    // what in the actual fuck is this vro
	std::vector<debug::Vertex> m_pending_debug_line_vertices;
	bool m_collecting_debug_lines = false;

	void beginDebugLineCollection(RenderFrame& frame);
	void endDebugLineCollection();

	std::mutex m_pending_post_settings_mutex;
	std::optional<PostProcessSettings> m_pending_post_settings;

	const toast::INodeOwner* m_render_owner_filter = nullptr;

	struct VoxelHighlightKey {
		const toast::VoxelNode* node = nullptr;
		uint64_t part = 0;

		auto operator==(const VoxelHighlightKey&) const noexcept -> bool = default;
	};

	struct VoxelHighlightKeyHash {
		auto operator()(const VoxelHighlightKey& key) const noexcept -> size_t {
			return std::hash<const void*> {}(key.node) ^ (std::hash<uint64_t> {}(key.part) * 0x9E3779B97F4A7C15ull);
		}
	};

	std::unordered_map<VoxelHighlightKey, const voxel::Volume*, VoxelHighlightKeyHash> m_voxel_highlights;
	const toast::INodeOwner* m_secondary_owner = nullptr;
	glm::mat4 m_secondary_transform {1.0f};
	const toast::Node* m_secondary_hidden = nullptr;

	[[nodiscard]]
	auto ownerTransform(const toast::Node& node) const -> const glm::mat4*;

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

	mutable std::mutex m_voxel_collision_mutex;
	std::shared_ptr<const VoxelCollisionWorld> m_voxel_collision_world;

	struct VoxelSceneKey {
		uint64_t node_rid = 0;
		uint32_t revision = 0;
		uint32_t content = 0;
		const voxel::Palette* palette = nullptr;

		uint32_t palette_revision = 0;

		[[nodiscard]]
		auto operator==(const VoxelSceneKey&) const -> bool = default;
	};

	std::vector<VoxelSceneKey> m_voxel_scene_key;

	VoxelChangeTracker m_voxel_change_tracker;

	/// Describes the newest storage whether published or pending so a patch starts from it
	VoxelScenePatcher m_voxel_patcher;

	std::unordered_map<uint64_t, glm::mat4> m_voxel_previous_models;

	/// Swaps into m_voxel_previous_models unless this frame is a probe capture
	std::unordered_map<uint64_t, glm::mat4> m_voxel_drawn_models;
	std::unordered_map<uint64_t, glm::mat4> m_mesh_previous_models;
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

	/// Guarded by m_reflection_probe_mutex
	std::vector<toast::FogVolume*> m_fog_volume_nodes;
	std::vector<toast::FogVolume*> m_tick_fog_volumes;

	std::vector<toast::MeshNode*> m_tick_mesh_nodes;
	std::vector<toast::Light*> m_tick_light_nodes;
	std::vector<toast::Camera*> m_tick_camera_nodes;
	std::vector<toast::ReflectionProbe*> m_tick_probe_nodes;
	std::vector<std::pair<uint32_t, const toast::ReflectionProbe*>> m_tick_probes_by_distance;
	std::vector<PunctualShadowCandidate> m_tick_spot_candidates;
	std::vector<PunctualShadowCandidate> m_tick_point_candidates;

	/// Survive across ticks so a shadowed light keeps its layer and its cached map
	/// Sized to shadows::spotShadowSlots() and pointShadowSlots()
	std::vector<shadow_slots::Slot> m_spot_slots;
	std::vector<shadow_slots::Slot> m_point_slots;
	uint64_t m_shadow_displaced_total = 0;

	/// Padded sphere a cascade keeps until the camera slice leaves it
	struct CascadeAnchor {
		glm::vec3 center {0.0f};
		float radius = 0.0f;
		glm::vec3 light_direction {0.0f};
		uint32_t resolution = 0;
	};

	std::array<CascadeAnchor, shadows::k_cascade_count> m_cascade_anchors {};
	std::vector<shadow_slots::Candidate> m_tick_slot_candidates;
	std::vector<int32_t> m_tick_slot_of;

	/// Parallel to RenderFrame::lights until finalizeLights
	struct LightMeta {
		float importance = 0.0f;
		float depth_reach = 0.0f;
		bool visible = false;
	};

	std::vector<LightMeta> m_tick_light_meta;
	std::vector<uint32_t> m_tick_light_order;
	std::vector<GpuLight> m_tick_light_scratch;
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

	std::unique_ptr<SelectionOutline> m_selection_outline;

	std::unique_ptr<SkinningPass> m_skinning_pass;

	std::unique_ptr<RayTracingScene> m_ray_tracing_scene;

	std::unique_ptr<VoxelTraceScene> m_voxel_trace_scene;
	std::unique_ptr<VoxelBlasCache> m_voxel_blas_cache;
	/// Render thread scratch
	std::vector<VoxelTraceScene::Volume> m_trace_volumes;
	std::vector<uint32_t> m_trace_records;

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
	std::vector<FrameResources> m_fog_volume_res;

	std::optional<vma::raii::Buffer> m_blue_noise_buffer;
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
	std::atomic_bool m_debug_windows_visible {true};
	std::atomic_bool m_debug_overlay_in_game {false};
	std::atomic_bool m_game_view {false};
	std::atomic_bool m_debug_overlay_owns_cursor {false};
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
	return VulkanRenderer::instance->getCurrentFrameBuild();
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

inline void registerFogVolumeProxy(toast::FogVolume* node) {
	VulkanRenderer::instance->registerFogVolumeProxy(node);
}

inline void unregisterFogVolumeProxy(toast::FogVolume* node) {
	VulkanRenderer::instance->unregisterFogVolumeProxy(node);
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

}    // namespace renderer
