/// @file render_frame.hpp
/// @author dario
/// @date 30/10/2026

#pragma once

#include "toast/renderer/debug/debug_draw.hpp"
#include "toast/renderer/gpu_types.hpp"
#include "toast/renderer/settings/post_process_settings.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <toast/assets/core_types.hpp>
#include <toast/world/gizmo_layout.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace assets {
class Material;
}

namespace renderer {

class VulkanMesh;
class VoxelGpuStorage;

struct ShadowView {
	glm::mat4 view_projection {1.0f};

	glm::vec4 cull_sphere {0.0f};

	uint32_t layer = 0;
	uint32_t resolution = 0;
	bool directional = false;
};

struct ShadowFrame {
	std::vector<ShadowView> views;

	/// matrices[i] belongs to views[i]
	std::vector<glm::mat4> matrices;
};

/// Widest fields first since this is copied per instance every frame
struct MeshInstanceProxy {
	VulkanMesh* mesh = nullptr;
	assets::Material* material = nullptr;
	assets::Material* root_material = nullptr;
	uint64_t node_rid = 0;
	glm::mat4 model = glm::mat4(1.0f);

	glm::vec3 bounds_center {0.0f};
	float bounds_radius = 0.0f;

	// joint_count > 0 selects the skinned pipeline
	uint32_t joint_offset = 0;
	uint32_t joint_count = 0;

	uint32_t instance_index = 0;

	static constexpr uint32_t k_no_posed_vertices = ~0u;

	uint32_t posed_vertex_offset = k_no_posed_vertices;

	bool visible = true;

	/// Selected in the editor hierarchy
	bool outlined = false;
};

/// Widest fields first since this is copied per instance every frame
struct VoxelVolumeProxy {
	glm::mat4 model {1.0f};
	glm::mat4 inverse_model {1.0f};
	glm::mat4 previous_model {1.0f};

	uint64_t node_rid = 0;

	glm::uvec3 brick_dims {0};
	uint32_t record_index = 0;

	glm::vec3 bounds_center {0.0f};
	float bounds_radius = 0.0f;

	float view_distance = 0.0f;
	uint32_t highlight_record = 0;

	/// False the first frame this node_uid is drawn so the shader reports motion as not written instead of zero
	bool has_previous = false;

	bool camera_inside = false;

	bool mirrored = false;

	bool visible = true;

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

constexpr size_t k_max_size_handles = 6;

struct TransformGizmoDraw {
	bool visible = false;
	toast::GizmoTool tool = toast::GizmoTool::select;
	toast::GizmoHandle hover = toast::GizmoHandle::none;
	toast::GizmoHandle active = toast::GizmoHandle::none;
	glm::mat4 model {1.0f};

	float drag_scale_factor = 1.0f;

	std::array<SizeHandleDraw, k_max_size_handles> size_handles {};
	uint32_t size_handle_count = 0;
	float size_handle_scale = 1.0f;
};

struct GizmoState {
	glm::vec3 origin {0.0f};
	glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
	float drag_scale_factor = 1.0f;

	std::array<SizeHandleDraw, k_max_size_handles> size_handles {};
	uint32_t size_handle_count = 0;
	float size_handle_scale = 1.0f;

	bool visible = false;
	toast::GizmoTool tool = toast::GizmoTool::select;
	toast::GizmoHandle hover = toast::GizmoHandle::none;
	toast::GizmoHandle active = toast::GizmoHandle::none;
};

#if defined(TOAST_DEV_TOOLS)
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
#endif

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

	/// Nearest first and at most k_max_fog_volumes
	std::vector<FogVolumeGpu> fog_volumes;

	std::vector<debug::Vertex> debug_line_vertices;    // pairs form line segments
	std::vector<debug::Vertex> debug_triangle_vertices;
	std::vector<glm::mat4> debug_gizmo_instances;
	std::vector<debug::Billboard> debug_billboards;
	std::vector<debug::Mesh> debug_meshes;

	std::vector<vk::CommandBuffer> ui_command_buffers;
	std::vector<vk::ImageView> ui_output_views;
	std::vector<UIWorldPanelProxy> ui_world_panels;
	std::shared_ptr<const void> ui_slot_guard;

#if defined(TOAST_DEV_TOOLS)
	ImGuiInputSnapshot imgui_input;
#endif

	PostProcessSettings post_process;

	uint64_t sequence = 0;

	LightStats light_stats;

	TransformGizmoDraw transform_gizmo;

	/// Render resolution
	glm::vec2 viewport_extent {0.0f};
	glm::vec2 output_extent {0.0f};
	/// NDC offset jittered_view_projection adds
	glm::vec2 jitter_ndc {0.0f};

	/// Camera near plane distance so voxel cells it clips draw with the camera inside variant
	float near_plane = 0.0f;

	float camera_near = 0.01f;
	float camera_far = 5000.0f;

	float cluster_near = 0.1f;
	float cluster_far = 5000.0f;

	uint32_t render_mode = 0;

	int32_t probe_capture_index = -1;
	uint32_t probe_capture_face = 0;

	int32_t irradiance_capture_index = -1;
	uint32_t irradiance_capture_face = 0;

	uint32_t capture_extent = 0;

	/// Jittered and resolved by TaaPass which then skips FxaaPass
	bool taa_active = false;

	/// History is stale since the camera switched or TAA just turned on
	bool taa_reset = false;
};

}
