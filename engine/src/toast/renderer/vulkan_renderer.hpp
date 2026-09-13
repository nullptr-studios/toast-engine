/// @file VulkanRenderer.hpp
/// @author dario
/// @date 16/05/2026

#pragma once

#include "compute_pass_base.hpp"
#include "output_target_base.hpp"
#include "post_process_pass_base.hpp"
#include "post_process_settings.hpp"
#include "render_pass_base.hpp"
#include "shader_layout.hpp"
#include "shadow_constants.hpp"
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
class Light;
class ReflectionProbe;
class IrradianceVolume;
class PostProcessVolume;
class INodeOwner;
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

/// Main thread builds a RenderFrame and submits it; the render thread records and presents it
class VulkanRenderer {
public:
	[[nodiscard]]
	static auto selectDepthFormat(const VulkanCore& core) -> vk::Format;

	static constexpr uint32_t k_frames_in_flight = 3;

	static constexpr uint8_t k_render_frames = 3;    // Number of frames queued for rendering

	struct FrameContext {
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::CommandBuffer compute_command_buffer = nullptr;
		vk::raii::Semaphore image_available = nullptr;
		vk::raii::Semaphore compute_to_graphics = nullptr;    ///< signaled by the compute submit, waited on by the graphics submit
		vk::raii::Fence in_flight = nullptr;
		vk::raii::Fence compute_in_flight = nullptr;          ///< defense in depth, separate from in_flight per the review
		uint32_t last_image_index = 0;
		bool has_submitted = false;
		/// This frame rendered a probe cube face rather than the camera's view, so it is never published
		bool was_probe_capture = false;

		/// Per frame rather than per mesh: the slot's fence is exactly the lifetime scratch needs
		std::vector<vma::raii::Buffer> blas_scratch;
	};

	static constexpr uint32_t k_max_directional_lights = 4;

	/// Shared by every skinned instance in a frame. ~100 joints per character leaves room for dozens
	static constexpr uint32_t k_max_joint_matrices = 4096;

	/// @brief Capacity of the per-frame instance buffer; one entry per *drawn* proxy
	static constexpr uint32_t k_max_instances = 16384;

	/// Mirrors mesh.slang's InstanceData. Model matrix here rather than a push constant so proxies sharing a
	/// mesh and material batch into one instanced draw. 16-byte padded to match the shader's stride
	struct InstanceData {
		glm::mat4 model {1.0f};
		uint32_t joint_offset = 0;
		/// Scalars rather than a uvec3 - see the matching note in mesh.slang's InstanceData about std430
		/// giving a uint3 16-byte alignment and pushing the stride to 96
		std::array<uint32_t, 3> _pad0 {0, 0, 0};
	};

	static_assert(sizeof(InstanceData) == 80, "InstanceData stride must match mesh.slang's std430 layout");

	/// @brief One AmbientLight/DirectionalLight is unbounded/unculled, so it rides along in FrameUBO
	/// instead of going through the clustered PointLight/Spotlight system
	struct DirectionalLightData {
		glm::vec4 direction;          // world-space, w unused
		glm::vec4 color_intensity;    // rgb color, a intensity
	};

	/// Small on purpose: a probe corrects an already-correct fallback, so dropping the 8th costs nothing.
	/// A dropped light is simply absent
	static constexpr uint32_t k_max_reflection_probes = 8;

	/// @brief One ReflectionProbe, resolved to POD on the main thread like the lights
	struct ReflectionProbeData {
		glm::vec4 position_radius;    // xyz world position, w influence radius
		// xyz half-extents of the projection box, w intensity. Zero extents mean sphere projection, so the
		// shape choice rides in the data rather than needing a flag of its own
		glm::vec4 box_extents_intensity;

		// x = which cubemap this probe owns. Explicit because this array re-sorts by distance every frame while
		// the cubemaps are indexed by registration order - otherwise reflections swap as the camera moves
		glm::vec4 params {0.0f};
	};

	/// Volumes describe rooms, not objects, so a handful covers a level
	static constexpr uint32_t k_max_irradiance_volumes = 4;

	/// 256 KiB of coefficients. Probe spacing is bounded by bake time (six frames each), not by this
	static constexpr uint32_t k_max_irradiance_probes = 4096;

	/// Four SH coefficients average away anything finer, but 64 still catches a window or a lamp
	static constexpr uint32_t k_irradiance_capture_size = 64;

	/// @brief One IrradianceVolume, resolved to POD on the main thread
	struct IrradianceVolumeData {
		/// xyz world position of the grid's minimum corner, w spacing between probes
		glm::vec4 min_corner_spacing {0.0f};
		/// xyz probe count per axis, w index of this volume's first probe in the SH buffer
		glm::uvec4 counts_base {0};
		/// xyz half-extents, w intensity
		glm::vec4 extents_intensity {0.0f};

		/// x = 1 once this volume's SH slice is its own. The buffer is shared and never cleared, so without
		/// this a fresh volume reads the previous scene's bake. Published even when 0 - the bake reads its base back
		glm::uvec4 baked_pad {0};
	};

	/// Aliased, not defined here, so PostProcessVolume can carry one without including the renderer
	using PostProcessSettings = renderer::PostProcessSettings;

	struct FrameUBO {
		glm::mat4 view {1.0f};
		glm::mat4 projection {1.0f};
		glm::mat4 view_projection {1.0f};

		glm::vec3 camera_position {};

		float time = 0.0f;

		// Unclustered lighting: nothing here has a position to cull by. Point and spot lights go through
		// RenderFrame::lights instead
		glm::vec4 ambient_color_intensity {0.0f};    // rgb color, a intensity; zero if there's no AmbientLight

		// x = directional_light_count, yzw unused padding. A scalar followed by a vec3 packs differently under
		// std140 than under HLSL cbuffer rules; one uvec4 sidesteps which convention Slang picks
		glm::uvec4 directional_light_count_pad {0};

		std::array<DirectionalLightData, k_max_directional_lights> directional_lights {};

		// 0 Lit, 1 ClusterHeatmap. In the frame UBO because MaterialPass's push-constant blob is baked
		// entirely from reflected material values - no room left for engine state
		glm::uvec4 render_mode_pad {0};

		// World -> shadow clip, refitted per tick to slices of the camera's near..shadowDistance sub-frustum
		std::array<glm::mat4, shadows::k_cascade_count> cascade_view_projection {};

		// Far distance of each cascade in view space; the shader picks one by comparing the fragment's
		// interpolated view-space depth against these
		glm::vec4 cascade_splits {0.0f};

		// World size of one texel in each cascade, which is what the normal-offset bias is scaled by - a
		// single value can't work when cascade 3 covers ~50x the ground area cascade 0 does
		glm::vec4 cascade_texel_world_size {0.0f};

		// Constant depth bias per cascade, in that cascade's normalized depth units
		glm::vec4 cascade_depth_bias {0.0f};

		// x = caster index + 1 (0 = nothing casts), y = cascades fitted, z/w = one texel in UV units.
		// Appended last so every offset above stays put for shaders mirroring only a prefix
		glm::vec4 shadow_params {0.0f};

		// x = 1 when the cubemaps hold real data, y = roughness levels in the prefiltered chain. Zero falls
		// back to treating AmbientLight as a uniform environment
		glm::vec4 environment_params {0.0f};

		// x = probe count, yzw unused padding - same uvec4 trick as the directional light count
		glm::uvec4 reflection_probe_count_pad {0};

		std::array<ReflectionProbeData, k_max_reflection_probes> reflection_probes {};

		// x = volume count, yzw padding. Appended after the probe array so every offset above it - and in the
		// shaders mirroring only a prefix of this struct - stays exactly where it was
		glm::uvec4 irradiance_volume_count_pad {0};

		std::array<IrradianceVolumeData, k_max_irradiance_volumes> irradiance_volumes {};

		// x = 1 to trace against the TLAS instead of sampling the maps. Always 0 without ray query, which is
		// what makes gScene safe to leave unwritten there. Set means tick() fits no cascades at all
		glm::vec4 traced_shadow_params {0.0f};
	};

	// The last member drifts most, since every addition lands there and a mismatch reads as a feature doing
	// nothing rather than as an error. Verify with: slangc -target spirv-assembly | grep OpMemberDecorate
	static_assert(
	    offsetof(FrameUBO, irradiance_volumes) == 1136, "FrameUBO::irradiance_volumes must match lighting.slang's std140 layout"
	);
	static_assert(
	    offsetof(FrameUBO, traced_shadow_params) == 1392, "FrameUBO::traced_shadow_params must match lighting.slang's std140 layout"
	);

	/// Mirrored by lighting.slang and cluster_lighting.slang. The culling shader reads only the first two
	/// members but indexes the same buffer, so it still has to agree on the full stride
	struct GpuLight {
		glm::vec4 world_pos_range;    // xyz world-space position, w = attenuation range (sphere radius, falloff distance)
		glm::vec4 view_pos_type;      // xyz view-space position (culling only), w = type (0=point,1=spot)
		glm::vec4 color_intensity;    // rgb color, a intensity
		glm::vec4 direction_pad;      // xyz world-space direction (spot only), w = shadow layer index, -1 when not casting
		glm::vec4 cone_angles;        // x = cos(outer), y = cos(inner), z = shadow near plane, w = shadow far plane

		// x = fraction of the layer this light rendered into, scaling a [0,1] lookup; y = point lights only,
		// exact 90-degree face over the guard-banded frustum actually rendered
		glm::vec4 shadow_atlas {1.0f, 1.0f, 0.0f, 0.0f};

		// World -> shadow clip space for a shadow-casting Spotlight. Unused by point lights, which need six
		// matrices and instead reconstruct depth analytically from cone_angles' near/far
		glm::mat4 shadow_view_projection {1.0f};
	};

	/// @brief One shadow map layer to render this frame, resolved main-thread-side
	struct ShadowView {
		glm::mat4 view_projection {1.0f};

		/// What this view can see, expanded to cover casters between it and the light. Without it, twenty
		/// views means twenty full scene redraws
		glm::vec4 cull_sphere {0.0f};

		uint32_t layer = 0;          ///< array layer within the target image
		bool directional = false;    ///< true = cascade image, false = punctual image

		/// Square pixel size actually rendered, as a sub-rect anchored at the layer's origin. Cascades always
		/// fill their layer; a punctual light shrinks with distance (shadows::punctualShadowResolution)
		uint32_t resolution = 0;
	};

	struct ShadowFrame {
		std::vector<ShadowView> views;

		/// Flattened matrix array uploaded to shadow_depth.slang's gShadow, indexed by the viewIndex push
		/// constant. views[i] renders with matrices[i], so the two stay parallel
		std::vector<glm::mat4> matrices;
	};

	struct MeshInstanceProxy {
		VulkanMesh* mesh = nullptr;
		assets::Material* material = nullptr;
		assets::Material* root_material = nullptr;
		glm::mat4 model = glm::mat4(1.0f);

		// joint_count > 0 selects the skinned pipeline variant and binds mesh's SkinVertex stream; the
		// matrices themselves live in RenderFrame::joint_matrices[joint_offset, joint_offset+joint_count)
		uint32_t joint_offset = 0;
		uint32_t joint_count = 0;

		// World-space bounding sphere, from the mesh's local one transformed by model. ShadowPass culls
		// casters against each shadow view with it; the colour pass uses it through `visible` below
		glm::vec3 bounds_center {0.0f};
		float bounds_radius = 0.0f;

		/// Against the frame's own camera, so a capture culls against its cube face rather than the viewport.
		/// ShadowPass ignores this and culls per view - geometry behind the camera still casts
		bool visible = true;

		/// Assigned after the sort, so proxies sharing a mesh and material are consecutive and draw as one
		/// instanced call. Culled proxies get none
		uint32_t instance_index = 0;

		static constexpr uint32_t k_no_posed_vertices = ~0u;

		/// Per instance, not per mesh - two characters sharing a rig hold different poses. k_no_posed_vertices
		/// on anything static, and past the buffer cap, which then draws its bind pose
		uint32_t posed_vertex_offset = k_no_posed_vertices;

		/// The one thing here that is stable across frames: a persistent BLAS needs a key the snapshot does
		/// not invalidate. Map key only, never dereferenced
		uint64_t node_uid = 0;
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

	/// A Handle rather than a UID: the render thread reads gpuTexture() off it, so the asset must outlive the
	/// frame, and resolving a UID would touch the asset manager from that thread
	struct DebugBillboard {
		glm::vec3 position {0.0f};
		float size = 1.0f;    ///< world-space edge length
		glm::vec4 tint {1.0f};
		assets::Handle<assets::Texture> texture;
	};

	/// @brief An editor-only mesh drawn as debug draw

	struct DebugMesh {
		glm::mat4 model {1.0f};
		glm::vec4 tint {1.0f};
		assets::Handle<assets::Mesh> mesh;
	};

	/// @brief Editor selection's active gizmo
	struct TransformGizmoDraw {
		bool visible = false;
		toast::GizmoTool tool = toast::GizmoTool::select;
		glm::mat4 model {1.0f};
		toast::GizmoHandle hover = toast::GizmoHandle::none;
		toast::GizmoHandle active = toast::GizmoHandle::none;
		float drag_scale_factor = 1.0f;    ///< scale tool only: live multiplicative factor for the active handle
	};

	                                     /// @brief Main-thread-only input to TransformGizmoDraw's, set via setGizmoState()
	struct GizmoState {
		bool visible = false;
		toast::GizmoTool tool = toast::GizmoTool::select;
		glm::vec3 origin {0.0f};
		glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
		toast::GizmoHandle hover = toast::GizmoHandle::none;
		toast::GizmoHandle active = toast::GizmoHandle::none;
		float drag_scale_factor = 1.0f;
	};

	/// SDL keycode encoding, same as event::WindowKey::key. DebugPass maps it to an ImGuiKey on the render
	/// thread, which keeps ImGui types out of this widely-included header
	struct ImGuiKeyEvent {
		int32_t key = 0;
		bool down = false;
	};

	/// Resolved main-thread-side from the window events. ImGui is not thread-safe, so the render thread never
	/// touches raw input
	struct ImGuiInputSnapshot {
		glm::vec2 mouse_pos {-1.0f, -1.0f};    // -1,-1 = outside window, matches ImGui's own convention
		std::array<bool, 3> mouse_down {};     // 0=left, 1=right, 2=middle - matches ImGui's own indexing
		float mouse_wheel_x = 0.0f;
		float mouse_wheel_y = 0.0f;
		std::vector<ImGuiKeyEvent> key_events;
		std::vector<uint32_t> char_events;    // typed unicode codepoints
	};

	struct RenderFrame {
		FrameUBO frame_data;

		std::vector<MeshInstanceProxy> mesh_instances;

		/// @brief Keeps every asset this frame points at alive until the frame has been rendered
		std::vector<assets::HandleBase> asset_refs;

		// PointLight/Spotlight entries; inert until Stage 3b's clustered light culling consumes it
		std::vector<GpuLight> lights;

		// Every shadow map layer to render this frame - directional cascades plus one view per shadow-casting
		// Spotlight and six per shadow-casting PointLight. Consumed by ShadowPass
		ShadowFrame shadows;

		// One shared pool, sliced by MeshInstanceProxy::joint_offset/joint_count
		std::vector<glm::mat4> joint_matrices;

		/// Per-instance data for every drawn proxy, indexed by MeshInstanceProxy::instance_index
		std::vector<InstanceData> instance_data;

		/// A second copy because the sets differ: instance_data holds only camera-visible proxies, packed for
		/// long instanced runs, while a shadow view draws casters the camera cannot see
		std::vector<InstanceData> shadow_instance_data;

		/// @brief Half-open range of mesh_instances belonging to one root material
		struct MaterialRange {
			assets::Material* root_material = nullptr;
			uint32_t begin = 0;
			uint32_t end = 0;
		};

		/// Without it every MaterialPass scans the whole proxy list for its own: Bistro's 130 materials means
		/// 130 full passes to discard almost everything each time
		std::vector<MaterialRange> material_ranges;

		// Resolved main-thread-side so ClusterLightingPass::update() never touches Camera/output-target
		// state directly from the render thread - same POD-snapshot pattern as everything else here
		glm::vec2 viewport_extent {0.0f};
		float camera_near = 0.01f;
		float camera_far = 5000.0f;

		ImGuiInputSnapshot imgui_input;

		// Viewport shading mode from the toolbar's "Mode" dropdown - 0 Lit, 1 ClusterHeatmap. Copied into
		// FrameUBO::render_mode_pad each tick() so material shaders can read it directly
		uint32_t render_mode = 0;

		/// Ordering should hold by construction - FIFO queue, semaphore-gated slot reuse. This exists because
		/// "should" is not evidence; the render thread warns if an id goes backwards
		uint64_t sequence = 0;

		// Post-process parameters for this frame; each post pass reads its own section - see PostProcessSettings
		PostProcessSettings post_process;

		// Which probe face this frame is capturing, -1 when it is an ordinary frame. When set, view/projection
		// above were overridden to look along that face and the finished scene colour is blitted into it
		int32_t probe_capture_index = -1;
		uint32_t probe_capture_face = 0;

		// Same for an irradiance probe. A frame is one or the other, never both - they share one staging cube
		int32_t irradiance_capture_index = -1;
		uint32_t irradiance_capture_face = 0;

		/// 0 on an ordinary frame. A capture ends up in a cube face this size, so rendering the full viewport
		/// paid for pixels that were then averaged away - 99% of the fragment work at 1080x720 into 64px
		uint32_t capture_extent = 0;

		// Immediate-mode debug draw data queued via debugDrawLine()/debugDrawBox()/debugDrawSphere()/
		// debugDrawAxes() dnd consumed by DebugPass
		std::vector<DebugVertex> debug_line_vertices;    // consecutive pairs; each pair is one line segment
		std::vector<glm::mat4> debug_gizmo_instances;    // one axis-triad gizmo draw per entry
		std::vector<DebugBillboard> debug_billboards;    // one camera-facing textured quad per entry
		std::vector<DebugMesh> debug_meshes;             // one editor furniture mesh per entry

		TransformGizmoDraw transform_gizmo;

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

	void submitFrame() noexcept;

	/// @brief Builds and submits the next RenderFrame; does nothing when no render slot is free
	void tick(float time) noexcept;

	/// @brief Registers @p node so its mesh is drawn each frame; no-op if already registered
	void registerMeshNodeProxy(toast::MeshNode* node);

	/// @brief Unregisters @p node so it stops being drawn
	void unregisterMeshNodeProxy(toast::MeshNode* node);

	/// @brief Registers @p node so it's collected into the per-frame light list each frame; no-op if already registered
	void registerLightNodeProxy(toast::Light* node);

	/// @brief Unregisters @p node so it stops contributing to lighting
	void unregisterLightNodeProxy(toast::Light* node);

	/// @brief Registers a Camera so the editor can draw its body as furniture
	void registerCameraNodeProxy(toast::Camera* node);

	void unregisterCameraNodeProxy(toast::Camera* node);

	/// @brief Registers @p node so it's collected into the per-frame probe list; no-op if already registered
	void registerReflectionProbeProxy(toast::ReflectionProbe* node);

	/// @brief Unregisters @p node so it stops contributing reflections
	void unregisterReflectionProbeProxy(toast::ReflectionProbe* node);

	/// @brief Registers @p node so its probe grid is collected for baking; no-op if already registered
	void registerIrradianceVolumeProxy(toast::IrradianceVolume* node);

	/// @brief Registers @p node so it grades the post chain while the camera is inside it
	void registerPostProcessVolumeProxy(toast::PostProcessVolume* node);

	/// @brief Unregisters @p node so it stops grading anything
	void unregisterPostProcessVolumeProxy(toast::PostProcessVolume* node);

	/// @brief Unregisters @p node so it stops contributing indirect diffuse
	void unregisterIrradianceVolumeProxy(toast::IrradianceVolume* node);

private:
	/// @brief Abandons an irradiance bake in flight; bases are derived from the live volume list, so adding or
	/// removing a volume renumbers them underneath it
	void cancelIrradianceBake();

public:
	/// @brief Caps draw/present rate in Hz; 0 or negative runs uncapped. Paces cached redraws too
	void setFrameRateLimit(double max_fps) noexcept { m_frame_rate_limit_hz.store(max_fps, std::memory_order_relaxed); }

	[[nodiscard]]
	auto frameRateLimit() const noexcept -> double {
		return m_frame_rate_limit_hz.load(std::memory_order_relaxed);
	}

	void stop();

	void addRenderPass(std::unique_ptr<IRenderPass> pass);

	void addComputePass(std::unique_ptr<IComputePass> pass);

	/// @brief Appends a post-process pass; they run in registration order, so the tonemap goes last
	void addPostProcessPass(std::unique_ptr<IPostProcessPass> pass);

	/// @brief Turns the post-process pass called @p name on or off
	/// @returns false when nothing answers to @p name - normal where it was never registered
	auto setPostProcessPassEnabled(std::string_view name, bool enabled) -> bool;

	/// @returns whether the pass called @p name exists and is enabled
	[[nodiscard]]
	auto isPostProcessPassEnabled(std::string_view name) const -> bool;

	/// @brief Format of the linear HDR scene target - world passes build against this, not the output format
	[[nodiscard]]
	auto getSceneColorFormat() const noexcept -> vk::Format {
		return m_scene_color_format;
	}

	/// @brief View of the HDR scene target, sampled by the post-process chain
	[[nodiscard]]
	auto getSceneColorView() const noexcept -> vk::ImageView {
		return m_scene_color.view.has_value() ? **m_scene_color.view : vk::ImageView {};
	}

	/// World normal .xyz, roughness .w. Forward shading emits radiance, leaving a post pass no surface to
	/// build a reflection ray from. Signed float, because world normals need the sign

	/// @brief View of the normal/roughness buffer
	[[nodiscard]]
	auto getSceneNormalView() const noexcept -> vk::ImageView {
		return m_scene_normal.view.has_value() ? **m_scene_normal.view : vk::ImageView {};
	}

	/// @brief View of the indirect-diffuse buffer, the term ambient occlusion is allowed to scale
	[[nodiscard]]
	auto getSceneIndirectView() const noexcept -> vk::ImageView {
		return m_scene_indirect.view.has_value() ? **m_scene_indirect.view : vk::ImageView {};
	}

	/// @brief Scene depth, sampled by screen-space passes
	///
	/// Left in eDepthReadOnlyOptimal, which is what lets the overlay stage depth-test after the post chain
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

	/// Enables/disables every pass matching @p name
	void setPassEnabled(std::string_view name, bool enabled);

	/// 1x1 white fallback for material texture slots without a ready texture
	[[nodiscard]]
	auto getDefaultTextureView() const noexcept -> vk::ImageView {
		return m_default_texture.getView();
	}

	/// @brief Which failsafe a slot should show, or null when there is nothing to report
	///
	/// The three cases are deliberately distinct images rather than one "broken" checkerboard: an artist
	/// looking at a wrong-looking wall needs to know whether the file is unreadable, the device rejected it,
	/// or the material points at a UID nothing resolves
	///
	/// @param has_reference The slot names a texture (as opposed to being left empty, which is not an error)
	/// @param texture The resolved GPU texture, or nullptr when the reference did not resolve
	[[nodiscard]]
	auto getFailsafeTextureView(bool has_reference, const VulkanTexture* texture) const noexcept -> vk::ImageView;

	/// @brief Nearest-filtered sampler the failsafe textures must be drawn with
	///
	/// They carry text and hard edges, so bilinear turns them into mush at any distance
	[[nodiscard]]
	auto getFailsafeSampler() const noexcept -> vk::Sampler {
		return *m_failsafe_sampler;
	}

	/// 1x1 black fallback - e.g. metallic maps, where "no texture" should mean fully dielectric
	[[nodiscard]]
	auto getDefaultBlackTextureView() const noexcept -> vk::ImageView {
		return m_default_black_texture.getView();
	}

	/// 1x1 flat tangent-space normal (0.5, 0.5, 1.0) fallback for normal maps
	[[nodiscard]]
	auto getDefaultNormalTextureView() const noexcept -> vk::ImageView {
		return m_default_normal_texture.getView();
	}

	[[nodiscard]]
	auto getDefaultSampler() const noexcept -> vk::Sampler {
		return *m_default_sampler;
	}

	/// @brief Shadow-map fallback: 1x1 depth array cleared to 1.0, so every lookup reads as lit
	///
	/// Depth format and array view are both forced by what a comparison sampler will accept
	[[nodiscard]]
	auto getDefaultShadowMapView() const noexcept -> vk::ImageView {
		return *m_default_shadow_view;
	}

	/// @brief Comparison sampler pairing with getDefaultShadowMapView(), see createShadowSampler()
	[[nodiscard]]
	auto getDefaultShadowSampler() const noexcept -> vk::Sampler {
		return *m_default_shadow_sampler;
	}

	/// @brief Environment fallback before the precompute runs
	///
	/// Black, so a wrong environment_params contributes nothing rather than full white everywhere
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

	/// @returns this frame's instance storage buffer, bound as gInstances in material set 0
	[[nodiscard]]
	auto getInstanceBuffer(uint32_t frame_index) const -> vk::Buffer {
		return frame_index < m_instance_res.size() && m_instance_res[frame_index].gpu_buffer.has_value()
		           ? **m_instance_res[frame_index].gpu_buffer
		           : vk::Buffer {};
	}

	/// @returns this frame's shadow instance storage buffer, bound as gInstances in ShadowPass's set 0
	[[nodiscard]]
	auto getShadowInstanceBuffer(uint32_t frame_index) const -> vk::Buffer {
		return frame_index < m_shadow_instance_res.size() && m_shadow_instance_res[frame_index].gpu_buffer.has_value()
		           ? **m_shadow_instance_res[frame_index].gpu_buffer
		           : vk::Buffer {};
	}

	/// @returns this frame's joint matrix storage buffer, bound as gJointMatrices in material set 0
	[[nodiscard]]
	auto getJointMatrixBuffer(uint32_t frame_index) const -> vk::Buffer {
		return frame_index < m_joint_matrix_res.size() && m_joint_matrix_res[frame_index].gpu_buffer.has_value()
		           ? **m_joint_matrix_res[frame_index].gpu_buffer
		           : vk::Buffer {};
	}

	// Expose raw descriptor pool handle so render passes can allocate their own descriptor sets
	[[nodiscard]]
	auto getDescriptorPoolHandle() const noexcept -> vk::DescriptorPool {
		return *m_descriptor_pool;
	}

	/// Set once at startup, so a MaterialPass built on any later frame can still bind the culled light buffers
	void setClusterLightingPass(const ClusterLightingPass* pass) noexcept { m_cluster_lighting_pass = pass; }

	[[nodiscard]]
	auto getClusterLightingPass() const noexcept -> const ClusterLightingPass* {
		return m_cluster_lighting_pass;
	}

	/// Same reason as setClusterLightingPass(): a MaterialPass built later still has to find the shadow map
	void setShadowPass(const ShadowPass* pass) noexcept { m_shadow_pass = pass; }

	[[nodiscard]]
	auto getShadowPass() const noexcept -> const ShadowPass* {
		return m_shadow_pass;
	}

	/// Main-thread-only, set once at startup for the same reason as the shadow pass: MaterialPass binds the
	/// environment cubemaps by name when its frame set is built, which can happen on any later frame
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

	/// @brief Queues a capture of every registered probe: six frames each, one per cube face
	void requestReflectionProbeBake() noexcept { m_probe_bake_requested.store(true, std::memory_order_release); }

	/// @brief Starts an irradiance volume bake; six frames per probe, and a volume holds many
	void requestIrradianceBake() noexcept { m_irradiance_bake_requested.store(true, std::memory_order_release); }

	/// @returns probes the registered irradiance volumes add up to, for the bake's cost estimate
	[[nodiscard]]
	auto getIrradianceProbeCount() const noexcept -> uint32_t {
		return m_irradiance_probe_total;
	}

	/// @returns volumes moved or re-spaced since their bake, or never baked. Surfaced because stale
	///          coefficients read as light leaking, not as stale data
	[[nodiscard]]
	auto getStaleIrradianceVolumeCount() const noexcept -> uint32_t {
		return m_irradiance_stale_count.load(std::memory_order_relaxed);
	}

	/// @returns frames drawn out of order. Should stay zero; non-zero means the FIFO reasoning is wrong
	[[nodiscard]]
	auto getOutOfOrderFrameCount() const noexcept -> uint32_t {
		return m_out_of_order_frames.load(std::memory_order_relaxed);
	}

	/// @brief Render frame interval over a rolling window, in milliseconds
	///
	/// Spread matters: wider than a refresh means frames land on a different number each time - judder
	void getFrameTimeStats(float& avg_ms, float& min_ms, float& max_ms) const noexcept {
		avg_ms = m_frame_time_avg_ms.load(std::memory_order_relaxed);
		min_ms = m_frame_time_min_ms.load(std::memory_order_relaxed);
		max_ms = m_frame_time_max_ms.load(std::memory_order_relaxed);
	}

	/// @returns ticks that advanced the simulation without producing a frame. Explains stutter with zero
	///          dropped frames - the camera moved during these but was sampled only on the ones that drew
	[[nodiscard]]
	auto getSkippedBuildCount() const noexcept -> uint32_t {
		return m_skipped_builds.load(std::memory_order_relaxed);
	}

	/// @returns frames built but never drawn. The motion in between is never shown, so it reads as stutter
	///          rather than as a dropped frame
	[[nodiscard]]
	auto getDroppedFrameCount() const noexcept -> uint32_t {
		return m_dropped_frames.load(std::memory_order_relaxed);
	}

	/// @returns mesh instances that survived frustum culling on the last tick
	[[nodiscard]]
	auto getVisibleInstanceCount() const noexcept -> uint32_t {
		return m_visible_instance_count.load(std::memory_order_relaxed);
	}

	/// @brief Draws bounding spheres and the culling frustum, green for kept and red for culled
	void setCullDebugDraw(bool enabled) noexcept { m_cull_debug_draw.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto getCullDebugDraw() const noexcept -> bool {
		return m_cull_debug_draw.load(std::memory_order_relaxed);
	}

	/// @brief Holds the culling frustum where it is, so the camera can move and inspect what got dropped
	void setCullFreeze(bool frozen) noexcept { m_cull_freeze.store(frozen, std::memory_order_relaxed); }

	[[nodiscard]]
	auto getCullFreeze() const noexcept -> bool {
		return m_cull_freeze.load(std::memory_order_relaxed);
	}

	/// @returns how many probes have moved since their capture, or have never been captured
	[[nodiscard]]
	auto getStaleReflectionProbeCount() const noexcept -> uint32_t {
		return m_probe_stale_count.load(std::memory_order_relaxed);
	}

	/// @brief Makes every material pass rebuild its engine-reserved frame descriptor set
	///
	/// Those sets cache image views written once at build time, so replacing an image must say so
	void requestMaterialFrameSetRebuild();

	/// @brief Non-const overload, for the render-thread callers that retune the sky
	/// @warning Render thread only - the environment rebuilds inside recordPre()
	[[nodiscard]]
	auto getEnvironmentPassMutable() const noexcept -> EnvironmentPass* {
		return m_environment_pass;
	}

	/// @returns the scene's top-level acceleration structure, or null without ray query support
	[[nodiscard]]
	auto getRayTracingScene() const noexcept -> RayTracingScene* {
		return m_ray_tracing_scene.get();
	}

	/// @returns instances the depth prepass laid down last frame; 0 when it is disabled or unsupported
	[[nodiscard]]
	auto getPrepassDrawnCount() const noexcept -> uint32_t;

	/// @returns the pass that poses skinned meshes, or null if it failed to build
	[[nodiscard]]
	auto getSkinningPass() const noexcept -> SkinningPass* {
		return m_skinning_pass.get();
	}

	/// @returns this frame's posed vertex buffer, bound as vertex stream 0 for a skinned draw
	[[nodiscard]]
	auto getPosedVertexBuffer(uint32_t frame_index) const -> vk::Buffer;

	/// @returns the per-skinned-instance acceleration structures, or null without ray query
	[[nodiscard]]
	auto getSkinnedBlasPool() const noexcept -> SkinnedBlasPool* {
		return m_skinned_blas_pool.get();
	}

	/// @returns whether @p material's pass built the alpha-cutout variant. The prepass asks because laying
	///          down depth for a discard-driven silhouette occludes what it discards
	[[nodiscard]]
	auto materialUsesCutout(assets::Material* material) const -> bool;

	/// @brief The probe pass, which also owns the irradiance SH buffer the material passes bind
	[[nodiscard]]
	auto getReflectionProbePassMutable() const noexcept -> ReflectionProbePass* {
		return m_reflection_probe_pass;
	}

	[[nodiscard]]
	void setActiveCamera(toast::Camera* camera);

	/// @brief Drops @p camera if it is the one being rendered from
	void forgetCamera(const toast::Camera* camera);

	[[nodiscard]]
	auto getCore() -> const VulkanCore& {
		return *m_core;
	}

	[[nodiscard]]
	auto getActiveCamera() -> toast::Camera* {
		return m_camera;
	}

	/// @brief Main-thread-only, resolved into RenderFrame::transform_gizmo at the top of the next tick()
	void setGizmoState(const GizmoState& state) noexcept { m_gizmo_state = state; }

	/// @brief Whether anything will actually draw the queued gizmos
	///
	/// Only the editor registers DebugPass, so a packaged game was building geometry nothing read - 144
	/// vertices per point light per frame. Off by default
	void setDebugDrawEnabled(bool enabled) noexcept { m_debug_draw_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto debugDrawEnabled() const noexcept -> bool {
		return m_debug_draw_enabled.load(std::memory_order_relaxed);
	}

	/// @brief Whether shaders trace their shadow rays instead of sampling the shadow maps
	///
	/// In the settings registry, not on a PostProcessVolume: a level has no opinion on the machine's GPU
	void setTracedShadowsEnabled(bool enabled) noexcept { m_traced_shadows_enabled.store(enabled, std::memory_order_relaxed); }

	[[nodiscard]]
	auto tracedShadowsEnabled() const noexcept -> bool {
		return m_traced_shadows_enabled.load(std::memory_order_relaxed);
	}

	/// @brief Main-thread-only, copied into RenderFrame::post_process at the top of the next tick()
	void setPostProcessSettings(const PostProcessSettings& settings) noexcept { m_post_process_settings = settings; }

	[[nodiscard]]
	auto getPostProcessSettings() const noexcept -> const PostProcessSettings& {
		return m_post_process_settings;
	}

	/// @brief Queues a settings change from off the main thread
	///
	/// DebugPass edits these from the render thread; assigning directly tears the struct against tick()
	void requestPostProcessSettings(const PostProcessSettings& settings) {
		const std::lock_guard lock(m_pending_post_settings_mutex);
		m_pending_post_settings = settings;
	}

	/// @brief Restricts rendering to nodes owned by @p owner; nullptr renders every owner. Main-thread only
	///
	/// Without it every open workspace draws into the one viewport. Changing it renumbers irradiance SH
	/// bases, so a bake in flight is abandoned
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
	// The last-drawn frame is kept as a local in mainRenderThread() rather than here - it is only ever touched
	// by that thread, and holding it as a member cost a second full copy of every RenderFrame
	const RenderFrame* m_rendering_frame = nullptr;

	std::counting_semaphore<k_render_frames> m_free_frames {k_render_frames};

	struct DepthResources {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> view;
	};

	/// @brief The linear HDR target the scene renders into, before tonemapping
	///
	/// One copy: written and consumed inside a single command buffer, and the frame fence gates the next
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

	/// @brief Hands the output target every frame whose GPU work has finished, oldest first
	///
	/// Called before waiting on a slot *and* right after submitting, so nothing waits for slot reuse
	void publishCompletedFrames();

	/// @brief Builds BLASes for meshes that became ready since the last frame
	///
	/// On the graphics buffer: a build needs a compute-capable queue and the upload path is transfer-only
	void recordAccelerationStructureBuilds(FrameContext& frame);

	/// @brief Builds the pipeline that copies the post chain's final image into the output image
	///
	/// Owned here so every post pass keeps one shape, and a fully disabled chain still shows the scene
	void createPresentResources();
	void createDescriptorPool();
	void createDefaultTexture();

	/// @brief Loads the three failsafe images straight off disk, bypassing the asset manifest
	///
	/// A failsafe that needs a working manifest to appear is no failsafe - these resolve by virtual path so
	/// a broken or stale database still shows them
	void createFailsafeTextures();
	void createDefaultShadowMap();
	void createDefaultCubemap();

	/// Creates a MaterialPass for every root material present in the frame
	void ensureMaterialPasses(RenderFrame& frame_data);

	void recordFrame(FrameContext& frame, uint32_t image_index) noexcept;

	/// @brief Records every MaterialPass, opaque then depth-sorted blended, into the open scene scope
	///
	/// Private rather than an interface - the ordering is mesh-material specific
	///
	/// @note Expects an open rendering scope and m_pass_mutex already held
	void recordMeshScene(vk::CommandBuffer cmd, uint32_t image_index);

	/// @brief One PointLight/Spotlight competing for a scarce punctual shadow slot
	///
	/// Ranked by distance, or a light behind the camera holds a slot the one lighting the shot needed
	struct PunctualShadowCandidate {
		size_t light_index = 0;
		float distance_squared = 0.0f;
		float resolution_scale = 1.0f;    ///< Light::shadowResolutionScale(), applied on top of the distance curve
	};

	/// @brief Fits this frame's shadow views - cascades, then punctual atlas slots - into @p frame
	///
	/// Main-thread-side so the render thread never touches Camera or Light state
	///
	/// @param shadow_caster_index Shadow-casting DirectionalLight, or negative when none casts
	/// @param spot_candidates,point_candidates Ranked by the caller; both are sorted in place here
	void fitShadowViews(
	    RenderFrame& frame, float aspect, int32_t shadow_caster_index, const glm::vec3& shadow_caster_direction,
	    std::vector<PunctualShadowCandidate>& spot_candidates, std::vector<PunctualShadowCandidate>& point_candidates
	);

	// Resource uploading
	//
	/// @brief One in-flight upload batch: a command buffer and the fence that says when it is done
	///
	/// Deliberately not part of FrameContext. Uploads used to record into the frame slot's transfer command
	/// buffer, which meant the buffer was reset every k_frames_in_flight frames whether or not its batch had
	/// finished - at editor frame rates that is a ~3ms window against a transfer that can take far longer,
	/// and resetting a pending command buffer is invalid usage. Upload rate has no reason to be tied to how
	/// many frames the renderer keeps in flight
	struct UploadSlot {
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::Fence fence = nullptr;
		bool in_flight = false;
	};

	/// Slots are taken round-robin and reclaimed in the same order, so the next one to take is always the
	/// oldest. Sized above k_frames_in_flight to keep streaming off the frame cadence, but not by much - a
	/// slot holds its jobs' staging buffers alive until its fence signals
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

	/// @brief Ceiling on host memory held by uploads that have been built but not yet consumed by the GPU
	///
	/// build() allocates a staging copy and, for a texture, the transcode scratch too. Dispatching every
	/// queued job the moment it arrives meant a scene load could stage its whole texture set at once - this
	/// project alone carries 728 MiB of ktx2. Jobs past the ceiling wait in m_upload_waiting instead
	static constexpr vk::DeviceSize k_upload_host_budget = 256ull << 20;

	/// Queued but not yet dispatched to the thread pool; guarded by m_upload_mutex
	std::deque<std::unique_ptr<PendingResourceUpload>> m_upload_waiting;
	std::atomic<vk::DeviceSize> m_upload_host_bytes {0};

	std::vector<std::unique_ptr<PendingResourceUpload>> m_upload_staging;
	std::queue<BatchedUploadGroup> m_pending_uploads;
	void createUploadRing();
	void pumpUploadQueue();
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
	std::vector<std::unique_ptr<IComputePass>> m_compute_passes;
	std::vector<std::unique_ptr<IPostProcessPass>> m_post_process_passes;

	/// One pass per root material
	std::unordered_map<assets::Material*, std::unique_ptr<MaterialPass>> m_material_passes;
	/// Guards m_material_passes + m_render_passes against listPasses()/setPassEnabled() from other threads
	mutable std::mutex m_pass_mutex;
	/// Set by ClearUnusedAssets
	std::atomic_bool m_pending_material_pass_clear {false};
	event::Listener m_asset_listener;

	/// Fallback material for meshes without one
	assets::Handle<assets::Material> m_default_material;
	bool m_default_material_warned = false;

	/// 1x1 white/black/flat-normal textures + shared sampler, every material pass's texture fallback -
	/// see MaterialRuntime::TextureSlot::default_fallback for which slot picks which one
	VulkanTexture m_default_texture;
	VulkanTexture m_fail_load_texture;
	VulkanTexture m_fail_gpu_texture;
	VulkanTexture m_missing_texture;
	vk::raii::Sampler m_failsafe_sampler = nullptr;
	VulkanTexture m_default_black_texture;
	VulkanTexture m_default_normal_texture;
	vk::raii::Sampler m_default_sampler = nullptr;

	/// 1x1 depth image cleared to 1.0 plus its comparison sampler - the shadow bindings' fallback when no
	/// ShadowPass is registered. See getDefaultShadowMapView()
	std::optional<vma::raii::Image> m_default_shadow_image;
	vk::raii::ImageView m_default_shadow_view = nullptr;
	vk::raii::Sampler m_default_shadow_sampler = nullptr;

	/// 1x1 black cubemap - see getDefaultCubeView()
	std::optional<vma::raii::Image> m_default_cube_image;
	vk::raii::ImageView m_default_cube_view = nullptr;

	/// Set once via setClusterLightingPass(); not owned here
	const ClusterLightingPass* m_cluster_lighting_pass = nullptr;

	/// Set once via setShadowPass(); owned by m_render_passes, not here
	const ShadowPass* m_shadow_pass = nullptr;

	/// Set once via setEnvironmentPass(); also owned by m_render_passes
	EnvironmentPass* m_environment_pass = nullptr;

	/// Set once via setReflectionProbePass(); owned by m_render_passes
	ReflectionProbePass* m_reflection_probe_pass = nullptr;

	std::atomic_bool m_probe_bake_requested {false};
	/// Probes whose capture no longer matches where they are; surfaced in the debug panel
	std::atomic<uint32_t> m_probe_stale_count {0};
	/// Cursor through the bake, in face steps: probe = cursor / 6, face = cursor % 6. Negative when idle
	int32_t m_probe_bake_cursor = -1;
	/// The automatic first bake has been attempted; retrying on failure would loop forever
	bool m_probe_auto_baked = false;
	vk::Format m_depth_format = vk::Format::eUndefined;
	DepthResources m_depth_resources;

	/// 16-bit float rather than 11/11/10: emissive materials go to intensity 64 and blending needs headroom
	/// above 1.0, and an alpha channel keeps the door open for passes that want coverage
	static constexpr vk::Format m_scene_color_format = vk::Format::eR16G16B16A16Sfloat;
	SceneColorResources m_scene_color;

	static constexpr vk::Format m_scene_normal_format = renderer::k_scene_normal_format;
	SceneColorResources m_scene_normal;

	static constexpr vk::Format m_scene_indirect_format = renderer::k_scene_indirect_format;
	SceneColorResources m_scene_indirect;

	/// Copies whatever the post chain produced into the output image - see createPresentResources()
	ShaderLayout m_present_layout;
	VulkanPipeline m_present_pipeline;
	vk::raii::Sampler m_present_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_present_sets;
	std::vector<vk::ImageView> m_present_bound_views;
	vk::ImageLayout m_depth_layout = vk::ImageLayout::eUndefined;

	TracyVkCtx m_tracy_vk_ctx = nullptr;    ///< Tracy GPU profiling context

	std::vector<FrameContext> m_frames;
	std::vector<vk::raii::Semaphore> m_render_finished_per_image;
	std::vector<vk::Fence> m_images_in_flight;
	std::vector<vk::ImageLayout> m_output_image_layouts;
	uint32_t m_current_frame = 0;

	/// Active camera for the renderer, Can be nullptr if no camera is set
	toast::Camera* m_camera = nullptr;

	/// Main-thread-only, written by setGizmoState(), read back inside tick() on the same thread
	GizmoState m_gizmo_state;

	/// @brief Blends every PostProcessVolume containing @p camera_position into one grade for this frame
	///
	/// Starts from m_post_process_settings, so a scene with no volume looks exactly as it did without them
	[[nodiscard]]
	auto blendPostProcessVolumes(const glm::vec3& camera_position) -> PostProcessSettings;

	/// Main-thread-only, same pattern as m_gizmo_state - see setPostProcessSettings()
	PostProcessSettings m_post_process_settings;

	/// Settings-driven, read on both threads - see setTracedShadowsEnabled()
	std::atomic_bool m_traced_shadows_enabled {false};

	/// See setDebugDrawEnabled(); read by every debugDraw* helper
	std::atomic_bool m_debug_draw_enabled {false};

	/// Set by requestPostProcessSettings() from any thread, drained into the above at the top of tick()
	std::mutex m_pending_post_settings_mutex;
	std::optional<PostProcessSettings> m_pending_post_settings;

	/// Main-thread-only, see setRenderOwnerFilter(). Non-owning; nullptr means "render every owner"
	const toast::INodeOwner* m_render_owner_filter = nullptr;

	/// Set by event::CaptureFrame, consumed once to wrap the next drawFrame() in whichever capture tool is
	/// attached
	event::Listener m_capture_listener;
	std::atomic_bool m_capture_frame_requested {false};

	/// Main-thread-only, accumulated from the window events and resolved into RenderFrame::imgui_input in
	/// tick() - the same pattern m_gizmo_state follows
	event::Listener m_imgui_input_listener;
	glm::vec2 m_imgui_mouse_pos {-1.0f, -1.0f};
	std::array<bool, 3> m_imgui_mouse_down {};
	float m_imgui_wheel_x_accum = 0.0f;
	float m_imgui_wheel_y_accum = 0.0f;
	std::vector<ImGuiKeyEvent> m_imgui_key_events_accum;
	std::vector<uint32_t> m_imgui_char_events_accum;

	/// Main-thread-only, set by event::SetRenderMode (the viewport toolbar's "Mode" dropdown), read back
	/// inside tick() on the same thread
	event::Listener m_render_mode_listener;
	uint32_t m_render_mode = 0;

	UIFrameBuilder m_ui_frame_builder;

	std::mutex m_mesh_proxy_mutex;
	std::vector<toast::MeshNode*> m_mesh_proxy_nodes;

	std::mutex m_light_proxy_mutex;
	std::vector<toast::Light*> m_light_proxy_nodes;

	std::mutex m_camera_proxy_mutex;
	std::vector<toast::Camera*> m_camera_proxy_nodes;

	std::mutex m_reflection_probe_mutex;
	std::vector<toast::ReflectionProbe*> m_reflection_probe_nodes;

	/// Guarded by m_reflection_probe_mutex - volumes are collected in the same place in tick() and baked
	/// through the same cube-capture path, so a second lock would only add a way for the two to disagree
	std::vector<toast::IrradianceVolume*> m_irradiance_volume_nodes;

	/// Guarded by m_reflection_probe_mutex, same as the probe and volume lists it is collected alongside
	std::vector<toast::PostProcessVolume*> m_post_process_volume_nodes;

	/// @brief Scratch buffers tick() refills each frame instead of reallocating
	///
	/// Clearing a member keeps the capacity, so a steady scene stops allocating here entirely
	std::vector<toast::MeshNode*> m_tick_mesh_nodes;
	std::vector<toast::Light*> m_tick_light_nodes;
	std::vector<toast::Camera*> m_tick_camera_nodes;
	std::vector<toast::ReflectionProbe*> m_tick_probe_nodes;
	std::vector<std::pair<uint32_t, const toast::ReflectionProbe*>> m_tick_probes_by_distance;
	std::vector<PunctualShadowCandidate> m_tick_spot_candidates;
	std::vector<PunctualShadowCandidate> m_tick_point_candidates;
	std::vector<toast::PostProcessVolume*> m_tick_post_volumes;

	/// Node behind each *published* volume slot. Publish order is not registration order - volumes get
	/// skipped - and the bake writes back by slot
	std::vector<toast::IrradianceVolume*> m_irradiance_published_nodes;

	/// Probes across every volume that fits, recomputed each tick
	uint32_t m_irradiance_probe_total = 0;

	/// Volumes whose cache has already been looked for; a miss means nothing was stored, not "try again"
	std::unordered_set<toast::IrradianceVolume*> m_irradiance_load_attempted;

	/// Grid each volume's coefficients were actually baked for, so a moved or re-spaced one can be detected.
	/// A volume absent from this map has never been baked at all, which counts as stale for the same reason
	std::unordered_map<toast::IrradianceVolume*, ShGridKey> m_irradiance_baked_keys;

	/// Volumes whose bake no longer matches their grid, recomputed each tick for the debug panel
	std::atomic<uint32_t> m_irradiance_stale_count {0};

	/// Instances that survived frustum culling this tick, for the debug panel's draw-count readout
	std::atomic<uint32_t> m_visible_instance_count {0};

	/// Monotonic frame id handed to VK_EXT_frame_boundary; render-thread only, never reset
	uint64_t m_frame_boundary_counter = 0;

	/// Stamped onto every RenderFrame by tick(); main-thread only
	uint64_t m_frame_sequence_counter = 0;

	/// Frame contexts submitted but not yet handed to the output target, in submission order
	std::deque<uint32_t> m_pending_publish;

	/// Owned directly rather than registered as an IRenderPass: it needs its own rendering scope before the
	/// scene's, which the generic pass stages have no way to express
	std::unique_ptr<DepthPrepass> m_depth_prepass;

	/// Owned directly for the same reason, and recorded before every other pass: its output is the vertex
	/// stream they read for skinned instances
	std::unique_ptr<SkinningPass> m_skinning_pass;

	/// The TLAS everything traceable lives in. Null when the device has no ray query support
	std::unique_ptr<RayTracingScene> m_ray_tracing_scene;

	/// One BLAS per skinned instance, refit each frame against SkinningPass's output. Null without ray query
	std::unique_ptr<SkinnedBlasPool> m_skinned_blas_pool;

	/// Sequence of the last frame the render thread drew, and how many arrived out of order
	std::atomic<uint64_t> m_last_drawn_sequence {0};
	std::atomic<uint32_t> m_out_of_order_frames {0};
	std::atomic<uint32_t> m_dropped_frames {0};

	/// Main thread ticks that advanced the simulation but never produced a render frame
	std::atomic<uint32_t> m_skipped_builds {0};

	/// @brief Render-thread frame interval statistics over a short rolling window, in milliseconds
	///
	/// Spread matters more than average: with no vsync a frame time wandering across a refresh interval is
	/// shown for one, two or three of them in turn
	std::atomic<float> m_frame_time_avg_ms {0.0f};
	std::atomic<float> m_frame_time_min_ms {0.0f};
	std::atomic<float> m_frame_time_max_ms {0.0f};

	/// Draws every instance's bounding sphere, green when it survived culling and red when it did not
	std::atomic_bool m_cull_debug_draw {false};

	/// @brief Holds the culling frustum still while the camera keeps moving
	std::atomic_bool m_cull_freeze {false};
	glm::mat4 m_frozen_cull_view_projection {1.0f};
	bool m_has_frozen_cull = false;

	/// Face being captured, or -1 when no irradiance bake is running
	int32_t m_irradiance_bake_cursor = -1;
	std::atomic_bool m_irradiance_bake_requested {false};

	/// True while either bake is walking faces. Read by the render thread, which drops its frame rate cap
	std::atomic_bool m_bake_active {false};
	/// Probes whose stored capture has already been looked for; a miss must not retry every frame
	std::unordered_set<toast::ReflectionProbe*> m_probe_load_attempted;

	std::vector<FrameUBO> m_frame_ubos;
	std::vector<FrameResources> m_frame_ubo_res;
	std::vector<FrameResources> m_joint_matrix_res;       ///< gJointMatrices storage buffer, one per frame in flight
	std::vector<FrameResources> m_instance_res;           ///< gInstances storage buffer, one per frame in flight
	std::vector<FrameResources> m_shadow_instance_res;    ///< ShadowPass's own instance buffer, see shadow_instance_data
	bool m_joint_matrix_pool_exhausted_warned = false;

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

/// @brief Null-safe: a camera can outlive the renderer on the way down
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

/// @WARN NOT THREAD SAFE
inline auto renderingFrame() -> const VulkanRenderer::RenderFrame* {
	return VulkanRenderer::instance->renderingFrame();
}

/// DEBUG LINES

/// @brief Queues a debug line segment for the frame currently being built
/// @note Call between beginFrameBuild() and submitFrame()
inline void debugDrawLine(glm::vec3 a, glm::vec3 b, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	auto& frame = VulkanRenderer::instance->beginFrameBuild();
	frame.debug_line_vertices.push_back({a, color});
	frame.debug_line_vertices.push_back({b, color});
}

/// @brief Queues a wireframe axis-aligned box for this frame
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

/// @brief Queues a wireframe sphere for this frame
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

/// @brief Queues an axis-triad gizmo at @p transform
inline void debugDrawAxes(const glm::mat4& transform) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	VulkanRenderer::instance->beginFrameBuild().debug_gizmo_instances.push_back(transform);
}

/// @brief Queues a camera-facing textured quad centred on @p world_position
/// @param size World-space edge length, so the quad shrinks with distance
/// @param texture Icon to draw; fully transparent texels are discarded by the shader
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

/// @brief Queues an editor debug mesh for this frame
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

/// @brief debugDrawMesh() overload taking a mesh asset UID, resolved on the calling thread
void debugDrawMesh(toast::UID mesh, const glm::mat4& transform, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// @brief debugDrawBillboard() overload taking a texture asset UID, resolved on the calling thread
/// @note will crash if UID does nt point to a texture
void debugDrawBillboard(glm::vec3 world_position, float size, toast::UID texture, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// @brief Queues an arrow pointing from @p from to @p
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

/// @brief Queues a wireframe cone
void debugDrawCone(
    glm::vec3 apex, glm::vec3 direction, float length, float half_angle_degrees, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
    int segments = 24
);

/// @brief Queues a wireframe frustum for @p camera
void debugDrawFrustum(
    const toast::Camera& camera, float aspect, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f}, float far_override = 0.0f
);

/// @brief Draws the frustum of an arbitrary view-projection
void debugDrawFrustumFromMatrix(const glm::mat4& view_projection, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f});

}    // namespace renderer
