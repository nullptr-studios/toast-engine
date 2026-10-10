/// @file debug_pass.cpp
/// @author dario
/// @date 10/06/2026

#include "debug_pass.hpp"

#include "../clustered_lighting_constants.hpp"
#include "../ray_tracing_scene.hpp"
#include "../shader_cache.hpp"
#include "../skinned_blas_pool.hpp"
#include "../voxel_debug.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_mesh.hpp"
#include "../vulkan_renderer.hpp"
#include "../vulkan_texture.hpp"
#include "cluster_lighting_pass.hpp"
#include "environment_pass.hpp"
#include "perf_window.hpp"
#include "shadow_pass.hpp"
#include "skinning_pass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <string>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/physics/narrow_phase.hpp>
#include <toast/physics/physics_settings.hpp>
#include <toast/physics/simulator.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {

constexpr std::array<toast::GizmoHandle, 3> k_axis_handles {
  toast::GizmoHandle::axis_x, toast::GizmoHandle::axis_y, toast::GizmoHandle::axis_z
};

const std::array<glm::vec4, 3> k_axis_colors {
  glm::vec4 {  1.0f, 0.086f, 0.349f, 1.0f}, // X
  glm::vec4 {  0.0f,   1.0f, 0.251f, 1.0f}, // Y
  glm::vec4 {0.161f, 0.678f,   1.0f, 1.0f}  // Z
};

void appendBox(std::vector<debug::Vertex>& out, glm::vec3 min, glm::vec3 max, glm::vec4 color) {
	const std::array<glm::vec3, 8> v {
	  glm::vec3 {min.x, min.y, min.z},
	  glm::vec3 {max.x, min.y, min.z},
	  glm::vec3 {max.x, max.y, min.z},
	  glm::vec3 {min.x, max.y, min.z},
	  glm::vec3 {min.x, min.y, max.z},
	  glm::vec3 {max.x, min.y, max.z},
	  glm::vec3 {max.x, max.y, max.z},
	  glm::vec3 {min.x, max.y, max.z},
	};

	static constexpr std::array<std::array<int, 4>, 6> faces {
	  {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7}}
	};

	for (const auto& f : faces) {
		out.push_back({v[f[0]], color});
		out.push_back({v[f[1]], color});
		out.push_back({v[f[2]], color});
		out.push_back({v[f[0]], color});
		out.push_back({v[f[2]], color});
		out.push_back({v[f[3]], color});
	}
}

// for the gizmos for resizing volumes
void appendSphere(std::vector<debug::Vertex>& out, float radius, int rings, int segments, glm::vec4 color) {
	const auto point = [&](int ring, int segment) {
		const float polar = glm::pi<float>() * static_cast<float>(ring) / static_cast<float>(rings);
		const float azimuth = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(segments);
		return glm::vec3(std::sin(polar) * std::cos(azimuth), std::sin(polar) * std::sin(azimuth), std::cos(polar)) * radius;
	};
	for (int ring = 0; ring < rings; ++ring) {
		for (int segment = 0; segment < segments; ++segment) {
			const glm::vec3 a = point(ring, segment);
			const glm::vec3 b = point(ring + 1, segment);
			const glm::vec3 c = point(ring + 1, segment + 1);
			const glm::vec3 d = point(ring, segment + 1);
			out.push_back({a, color});
			out.push_back({b, color});
			out.push_back({c, color});
			out.push_back({a, color});
			out.push_back({c, color});
			out.push_back({d, color});
		}
	}
}

void appendShaftAlongAxis(std::vector<debug::Vertex>& out, int axis, float length, float half_size, glm::vec4 color) {
	glm::vec3 min {-half_size, -half_size, -half_size};
	glm::vec3 max {half_size, half_size, half_size};
	min[axis] = 0.0f;
	max[axis] = length;
	appendBox(out, min, max, color);
}

void appendPyramidAlongAxis(
    std::vector<debug::Vertex>& out, int axis, float base_pos, float apex_pos, float half_size, glm::vec4 color
) {
	const int u = (axis + 1) % 3;
	const int w = (axis + 2) % 3;

	auto make = [&](float main, float along_u, float along_w) {
		glm::vec3 p {0.0f, 0.0f, 0.0f};
		p[axis] = main;
		p[u] = along_u;
		p[w] = along_w;
		return p;
	};

	const glm::vec3 apex = make(apex_pos, 0.0f, 0.0f);
	const std::array<glm::vec3, 4> base {
	  make(base_pos, half_size, half_size),
	  make(base_pos, half_size, -half_size),
	  make(base_pos, -half_size, -half_size),
	  make(base_pos, -half_size, half_size),
	};

	for (int i = 0; i < 4; ++i) {
		const int j = (i + 1) % 4;
		out.push_back({apex, color});
		out.push_back({base[i], color});
		out.push_back({base[j], color});
	}

	out.push_back({base[0], color});
	out.push_back({base[2], color});
	out.push_back({base[1], color});
	out.push_back({base[0], color});
	out.push_back({base[3], color});
	out.push_back({base[2], color});
}

void appendQuad(std::vector<debug::Vertex>& out, int axis, float offset, float size, glm::vec4 color) {
	const int u = (axis + 1) % 3;
	const int w = (axis + 2) % 3;

	auto make = [&](float along_u, float along_w) {
		glm::vec3 p {0.0f, 0.0f, 0.0f};
		p[u] = along_u;
		p[w] = along_w;
		return p;
	};

	const glm::vec3 a = make(offset, offset);
	const glm::vec3 b = make(offset + size, offset);
	const glm::vec3 c = make(offset + size, offset + size);
	const glm::vec3 d = make(offset, offset + size);

	out.push_back({a, color});
	out.push_back({b, color});
	out.push_back({c, color});
	out.push_back({a, color});
	out.push_back({c, color});
	out.push_back({d, color});
}

void appendRing(std::vector<debug::Vertex>& out, int axis, float radius, float thickness, int segments, glm::vec4 color) {
	const int u = (axis + 1) % 3;
	const int w = (axis + 2) % 3;
	const float inner = radius - (thickness * 0.5f);
	const float outer = radius + (thickness * 0.5f);

	auto make = [&](float r, float angle) {
		glm::vec3 p {0.0f, 0.0f, 0.0f};
		p[u] = r * std::cos(angle);
		p[w] = r * std::sin(angle);
		return p;
	};

	for (int i = 0; i < segments; ++i) {
		const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * glm::two_pi<float>();
		const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * glm::two_pi<float>();

		const glm::vec3 i0 = make(inner, a0);
		const glm::vec3 o0 = make(outer, a0);
		const glm::vec3 i1 = make(inner, a1);
		const glm::vec3 o1 = make(outer, a1);

		out.push_back({i0, color});
		out.push_back({o0, color});
		out.push_back({o1, color});
		out.push_back({i0, color});
		out.push_back({o1, color});
		out.push_back({i1, color});
	}
}

}    // namespace

namespace {

auto acquireShader(std::string_view uri) -> std::shared_ptr<const ShaderCache::Entry> {
	const auto uid = assets::resolveURI(uri);
	if (!uid.has_value()) {
		TOAST_ERROR("Render", "DebugPass shader not found in the asset manifest: {}", uri);
		return nullptr;
	}
	return ShaderCache::get().acquire(*uid);
}

}

DebugPass::DebugPass(
    const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent,
    const ClusterLightingPass* cluster_lighting_pass
)
    : m_cluster_lighting_pass(cluster_lighting_pass) {
	ZoneScoped;
	const auto shape_shader = acquireShader("core://shaders/debug_shape.slang");
	if (!shape_shader) {
		TOAST_ERROR("Render", "DebugPass has no usable shaders, the pass will draw nothing");
		return;
	}
	m_shader_layout.rebuild(core, shape_shader->reflection, "DebugPass");

	const vk::VertexInputBindingDescription debug_vertex_binding(0, sizeof(debug::Vertex), vk::VertexInputRate::eVertex);
	const std::vector<vk::VertexInputAttributeDescription> debug_vertex_attributes {
	  vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(debug::Vertex, position)),
	  vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(debug::Vertex, color)),
	};

	if (shape_shader) {
		VulkanPipeline::Config config;
		config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		config.debug_name = "DebugPass Lines";
		config.color_format = color_format;
		config.depth_format = depth_format;
		config.extent = extent;
		config.shader_spirv = shape_shader->spirv;
		config.pipeline_layout = *m_shader_layout.getPipelineLayout();
		config.vertex_bindings = {debug_vertex_binding};
		config.vertex_attributes = debug_vertex_attributes;
		config.topology = vk::PrimitiveTopology::eLineList;
		config.cull_mode = vk::CullModeFlagBits::eNone;
		config.depth_test = true;
		config.depth_write = false;
		config.blend_enable = true;
		m_line_pipeline.rebuild(core, config);
		config.debug_name = "DebugPass Fill";
		config.topology = vk::PrimitiveTopology::eTriangleList;
		m_fill_pipeline.rebuild(core, config);
	}

	if (shape_shader) {
		VulkanPipeline::Config config;
		config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		config.debug_name = "DebugPass Gizmo";
		config.color_format = color_format;
		config.depth_format = depth_format;
		config.extent = extent;
		config.shader_spirv = shape_shader->spirv;
		config.pipeline_layout = *m_shader_layout.getPipelineLayout();
		config.vertex_bindings = {debug_vertex_binding};
		config.vertex_attributes = debug_vertex_attributes;
		config.topology = vk::PrimitiveTopology::eTriangleList;
		config.cull_mode = vk::CullModeFlagBits::eNone;
		config.depth_test = false;
		config.depth_write = false;
		config.blend_enable = false;
		m_gizmo_pipeline.rebuild(core, config);
	}

	if (shape_shader) {
		VulkanPipeline::Config config;
		config.pipeline_type = VulkanPipeline::PipelineType::graphics;
		config.debug_name = "DebugPass Mesh";
		config.color_format = color_format;
		config.depth_format = depth_format;
		config.extent = extent;
		config.shader_spirv = shape_shader->spirv;
		config.pipeline_layout = *m_shader_layout.getPipelineLayout();
		config.vertex_entry = "vertexMesh";
		config.fragment_entry = "fragmentMesh";
		config.vertex_bindings = {vertexBindingDescription()};
		const auto mesh_attributes = vertexAttributeDescriptions();
		config.vertex_attributes = {mesh_attributes[0], mesh_attributes[1], mesh_attributes[4]};
		config.topology = vk::PrimitiveTopology::eTriangleList;
		config.cull_mode = vk::CullModeFlagBits::eBack;
		config.depth_test = true;
		config.depth_write = true;
		config.blend_enable = false;
		m_mesh_pipeline.rebuild(core, config);
	}

	createResources(core);
	createBillboardResources(core, color_format, depth_format, extent);
	initImGui(core, color_format, depth_format);
}

DebugPass::~DebugPass() {
	if (m_imgui_ready) {
		ImGui_ImplVulkan_Shutdown();
		ImGui::DestroyContext();
	}
}

namespace {

auto sdlKeyToImGuiKey(int32_t key) -> ImGuiKey {
	constexpr int32_t k_scancode_mask = 1 << 30;

	if (key >= 'a' && key <= 'z') {
		return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'a'));
	}
	if (key >= '0' && key <= '9') {
		return static_cast<ImGuiKey>(ImGuiKey_0 + (key - '0'));
	}

	switch (key) {
		case 32: return ImGuiKey_Space;
		case 13: return ImGuiKey_Enter;
		case 27: return ImGuiKey_Escape;
		case 8: return ImGuiKey_Backspace;
		case 9: return ImGuiKey_Tab;
		case 127: return ImGuiKey_Delete;
		default: break;
	}

	if ((key & k_scancode_mask) != 0) {
		switch (key & ~k_scancode_mask) {
			case 79: return ImGuiKey_RightArrow;
			case 80: return ImGuiKey_LeftArrow;
			case 81: return ImGuiKey_DownArrow;
			case 82: return ImGuiKey_UpArrow;
			case 225: return ImGuiKey_LeftShift;
			case 229: return ImGuiKey_RightShift;
			case 224: return ImGuiKey_LeftCtrl;
			case 228: return ImGuiKey_RightCtrl;
			case 226: return ImGuiKey_LeftAlt;
			case 230: return ImGuiKey_RightAlt;
			case 58: return ImGuiKey_F1;
			case 59: return ImGuiKey_F2;
			case 60: return ImGuiKey_F3;
			case 61: return ImGuiKey_F4;
			case 62: return ImGuiKey_F5;
			case 63: return ImGuiKey_F6;
			case 64: return ImGuiKey_F7;
			case 65: return ImGuiKey_F8;
			case 66: return ImGuiKey_F9;
			case 67: return ImGuiKey_F10;
			case 68: return ImGuiKey_F11;
			case 69: return ImGuiKey_F12;
			default: break;
		}
	}

	return ImGuiKey_None;
}

auto clusterHeatmapColorImGui(uint32_t light_count) -> ImVec4 {
	constexpr float k_heatmap_max_lights = 8.0f;
	const float t = std::clamp(static_cast<float>(light_count) / k_heatmap_max_lights, 0.0f, 1.0f);

	const ImVec4 c0(0.0f, 0.0f, 1.0f, 1.0f);
	const ImVec4 c1(0.0f, 1.0f, 0.0f, 1.0f);
	const ImVec4 c2(1.0f, 1.0f, 0.0f, 1.0f);
	const ImVec4 c3(1.0f, 0.0f, 0.0f, 1.0f);

	auto lerp = [](const ImVec4& a, const ImVec4& b, float u) {
		return ImVec4(a.x + ((b.x - a.x) * u), a.y + ((b.y - a.y) * u), a.z + ((b.z - a.z) * u), 1.0f);
	};

	if (t < 0.333f) {
		return lerp(c0, c1, t / 0.333f);
	}
	if (t < 0.667f) {
		return lerp(c1, c2, (t - 0.333f) / 0.334f);
	}
	return lerp(c2, c3, (t - 0.667f) / 0.333f);
}

}    // namespace

void DebugPass::update(uint32_t frame_index, float dt) {
	ZoneScoped;
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr || frame_index >= m_line_vertex_buffers.size()) {
		return;
	}

	if (m_imgui_ready) {
		ImGuiIO& io = ImGui::GetIO();
		const auto& input = frame->imgui_input;

		io.DisplaySize = ImVec2(frame->viewport_extent.x, frame->viewport_extent.y);
		io.DeltaTime = std::max(dt, 0.0001f);

		if (input.mouse_pos.x >= 0.0f && input.mouse_pos.y >= 0.0f) {
			io.AddMousePosEvent(input.mouse_pos.x, input.mouse_pos.y);
		}
		for (size_t i = 0; i < input.mouse_down.size(); ++i) {
			io.AddMouseButtonEvent(static_cast<int>(i), input.mouse_down[i]);
		}
		if (input.mouse_wheel_x != 0.0f || input.mouse_wheel_y != 0.0f) {
			io.AddMouseWheelEvent(input.mouse_wheel_x, input.mouse_wheel_y);
		}
		for (const auto& key_event : input.key_events) {
			const ImGuiKey imgui_key = sdlKeyToImGuiKey(key_event.key);
			if (imgui_key != ImGuiKey_None) {
				io.AddKeyEvent(imgui_key, key_event.down);
			}
		}
		for (const uint32_t codepoint : input.char_events) {
			io.AddInputCharacter(codepoint);
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		if (!m_perf) {
			m_perf = std::make_unique<PerfWindow>();
		}
		m_perf->draw();

		if (!m_voxels) {
			m_voxels = std::make_unique<voxel_debug::Monitor>();
		}
		m_voxels->update(*frame);

		if (m_editor_panels && ImGui::Begin("Toast Debug")) {
			{
				const uint32_t dropped = VulkanRenderer::instance->getDroppedFrameCount();
				const uint32_t out_of_order = VulkanRenderer::instance->getOutOfOrderFrameCount();
				if (out_of_order > 0) {
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Frames out of order: %u", out_of_order);
				}

				ImGui::TextDisabled("Frames dropped: %u", dropped);

				bool clamp_to_simulation = VulkanRenderer::instance->clampToSimulation();
				if (ImGui::Checkbox("Clamp to game thread", &clamp_to_simulation)) {
					VulkanRenderer::instance->setClampToSimulation(clamp_to_simulation);
				}

				float cap = static_cast<float>(VulkanRenderer::instance->frameRateLimit());
				if (ImGui::SliderFloat("FPS cap", &cap, 0.0f, 144.0f, cap <= 0.0f ? "uncapped" : "%.0f")) {
					VulkanRenderer::instance->setFrameRateLimit(static_cast<double>(cap));
				}
			}

			ImGui::TextDisabled("Depth prepass: %u instances", VulkanRenderer::instance->getPrepassDrawnCount());

			if (const auto* shadows = VulkanRenderer::instance->getShadowPass(); shadows != nullptr) {
				ImGui::TextDisabled(
				    "Shadow pass: %u draws, %u scopes, %u cached, %u spared",
				    shadows->getDrawCount(),
				    shadows->getPassCount(),
				    shadows->getCachedCount(),
				    shadows->getVoxelSparedCount()
				);

				if (const auto* frame = VulkanRenderer::instance->renderingFrame(); frame != nullptr) {
					const auto& stats = frame->light_stats;
					ImGui::TextDisabled(
					    "Shadow slots: %u of %u spot, %u of %u point, %llu displaced",
					    stats.spot_shadows,
					    renderer::shadows::k_max_spot_shadows,
					    stats.point_shadows,
					    renderer::shadows::k_max_point_shadows,
					    static_cast<unsigned long long>(stats.shadow_displaced)
					);
				}
			}

			if (const auto* skinning = VulkanRenderer::instance->getSkinningPass(); skinning != nullptr) {
				const auto* pool = VulkanRenderer::instance->getSkinnedBlasPool();
				ImGui::TextDisabled(
				    "Skinning: %u posed, %u skinned BLAS refit",
				    skinning->getPosedInstanceCount(),
				    pool != nullptr ? pool->getRecordedCount() : 0u
				);
			}

			if (auto* rt = VulkanRenderer::instance->getRayTracingScene(); rt != nullptr) {
				const uint32_t traced = rt->getInstanceCount();
				if (traced == 0) {
					ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "TLAS: empty - nothing to trace against");
				} else {
					ImGui::TextDisabled("TLAS: %u instances", traced);
				}

				bool trace_all_lights = VulkanRenderer::instance->tracedShadowsEnabled();
				if (ImGui::Checkbox("Traced shadows (all lights)", &trace_all_lights)) {
					VulkanRenderer::instance->setTracedShadowsEnabled(trace_all_lights);
				}
			} else {
				ImGui::TextDisabled("TLAS: ray query unsupported");
			}

			{
				bool cull_debug = VulkanRenderer::instance->getCullDebugDraw();
				if (ImGui::Checkbox("Show culling volumes", &cull_debug)) {
					VulkanRenderer::instance->setCullDebugDraw(cull_debug);
				}

				bool cull_freeze = VulkanRenderer::instance->getCullFreeze();
				if (ImGui::Checkbox("Freeze cull frustum", &cull_freeze)) {
					VulkanRenderer::instance->setCullFreeze(cull_freeze);
				}
			}

			ImGui::Separator();

			if (auto* environment = VulkanRenderer::instance->getEnvironmentPassMutable(); environment != nullptr) {
				if (m_sky_intensity_ui < 0.0f) {
					m_sky_intensity_ui = environment->getSkyIntensity();
				}
				ImGui::InputText("Environment HDR", m_environment_uri_ui.data(), m_environment_uri_ui.size());
				ImGui::SameLine();
				if (ImGui::Button("Load")) {
					environment->setEnvironmentMap(std::string_view(m_environment_uri_ui.data()));
				}
				if (!environment->getEnvironmentMapUri().empty()) {
					ImGui::SameLine();
					if (ImGui::Button("Clear")) {
						environment->setEnvironmentMap("");
					}
				}

				ImGui::SliderFloat("Sky intensity", &m_sky_intensity_ui, 0.0f, 20.0f, "%.2f");

				if (ImGui::IsItemDeactivatedAfterEdit()) {
					environment->setSkyIntensity(m_sky_intensity_ui);
				}
			}

			{
				const uint32_t probe_count = frame->frame_data.reflection_probe_count_pad.x;
				ImGui::Text("Reflection probes: %u", probe_count);
				for (uint32_t i = 0; i < probe_count && i < 4; ++i) {
					const auto& probe = frame->frame_data.reflection_probes[i];
					const glm::vec3 position(probe.position_radius);
					const glm::vec3 extents(probe.box_extents_intensity);
					if (extents.x > 0.0f || extents.y > 0.0f || extents.z > 0.0f) {
						ImGui::BulletText(
						    "cube %d box +/-(%.0f, %.0f, %.0f) at (%.0f, %.0f, %.0f)",
						    static_cast<int>(probe.params.x),
						    extents.x,
						    extents.y,
						    extents.z,
						    position.x,
						    position.y,
						    position.z
						);
					} else {
						ImGui::BulletText(
						    "cube %d sphere r=%.0f at (%.0f, %.0f, %.0f)",
						    static_cast<int>(probe.params.x),
						    probe.position_radius.w,
						    position.x,
						    position.y,
						    position.z
						);
					}
				}
				ImGui::Text(
				    "Camera: (%.0f, %.0f, %.0f)",
				    frame->frame_data.camera_position.x,
				    frame->frame_data.camera_position.y,
				    frame->frame_data.camera_position.z
				);
			}

			if (ImGui::Button("Bake reflection probes")) {
				VulkanRenderer::instance->requestReflectionProbeBake();
			}
			ImGui::SameLine();
			if (const uint32_t stale = VulkanRenderer::instance->getStaleReflectionProbeCount(); stale > 0) {
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%u stale", stale);
			} else {
				ImGui::TextDisabled("(6 frames per probe)");
			}

			if (ImGui::CollapsingHeader("Screen-space reflections")) {
				const auto& ssr = frame->post_process.ssr;
				ImGui::TextDisabled("intensity %.2f, max roughness %.2f", ssr.intensity, ssr.max_roughness);
				ImGui::TextDisabled("stride %.3f m x %u steps", ssr.stride, ssr.max_steps);
				ImGui::TextDisabled("thickness %.2f m", ssr.thickness);
				ImGui::TextDisabled("Ray reach: %.1f m", ssr.stride * static_cast<float>(ssr.max_steps));
			}

			if (const uint32_t irradiance_probes = VulkanRenderer::instance->getIrradianceProbeCount(); irradiance_probes > 0) {
				if (ImGui::Button("Bake irradiance volumes")) {
					VulkanRenderer::instance->requestIrradianceBake();
				}
				ImGui::SameLine();
				if (const uint32_t stale = VulkanRenderer::instance->getStaleIrradianceVolumeCount(); stale > 0) {
					ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%u stale (%u probes)", stale, irradiance_probes);
				} else {
					ImGui::TextDisabled("(%u probes, %u frames)", irradiance_probes, irradiance_probes * 6);
				}
			}

			if (ImGui::CollapsingHeader("Ambient occlusion")) {
				const auto& ssao = frame->post_process.ssao;
				ImGui::TextDisabled("radius %.2f m, strength %.2f", ssao.radius, ssao.strength);
				ImGui::TextDisabled("range cutoff %.2f m, %u samples", ssao.range_cutoff, ssao.sample_count);
			}

			if (ImGui::CollapsingHeader("Clustered lighting")) {
				using namespace clustered_lighting;

				const auto& stats = frame->light_stats;
				ImGui::Text("Lights %zu of %u submitted", frame->lights.size(), stats.submitted);
				ImGui::TextDisabled("%u off screen, %u over the %u cap", stats.offscreen, stats.truncated, k_max_lights);
				ImGui::TextDisabled("Depth %.2f to %.1f m", frame->cluster_near, frame->cluster_far);

				if (m_cluster_lighting_pass != nullptr) {
					m_cluster_lighting_pass->requestGridReadback();

					const auto counts = m_cluster_lighting_pass->getClusterLightGridCounts(frame_index);
					if (counts.empty()) {
						ImGui::TextDisabled("Waiting for the cluster grid");
					} else {
						const auto grid = ClusterLightingPass::summarizeGrid(counts);
						ImGui::Text("Clusters %u of %u occupied", grid.occupied, k_cluster_count);
						ImGui::TextDisabled("Per cluster mean %.1f p95 %u max %u", grid.mean, grid.p95, grid.max_count);
						if (grid.overflowed > 0) {
							ImGui::TextColored(
							    ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
							    "%u clusters over %u lights, %u entries dropped",
							    grid.overflowed,
							    k_max_lights_per_cluster,
							    grid.dropped
							);
						}
					}
				}
			}

			m_voxels->drawPanel(*frame);

			if (ImGui::CollapsingHeader("Voxel physics")) {
				const physics::Simulator::PhysicsStepProfile& phys = physics::Simulator::stepProfile();

				ImGui::Text(
				    "Tick %6.2f ms  damage %.2f  connectivity %.2f  characters %.2f  narrow %.2f  solve %.2f",
				    phys.tick_ms,
				    phys.damage_apply_ms,
				    phys.connectivity_ms,
				    phys.character_step_ms,
				    phys.narrow_phase_ms,
				    phys.solve_ms
				);
				if (phys.ticks_this_frame > 1) {
					ImGui::TextColored(
					    ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
					    "The fixed step accumulator ran %zu ticks this frame trying to catch up, roughly %zu x the "
					    "numbers above, not one of them%s",
					    phys.ticks_this_frame,
					    phys.ticks_this_frame,
					    phys.ticks_capped_by_time_budget ? " (stopped by the burst time budget, not max_steps)" : ""
					);
				}
				ImGui::TextDisabled(
				    "%zu bodies (%zu awake), %zu voxel shapes, %zu manifolds, %zu constraints",
				    phys.body_count,
				    phys.awake_body_count,
				    phys.voxel_shape_count,
				    phys.manifold_count,
				    phys.constraints
				);
				if (phys.character_sweep_calls > 0 || phys.character_overlap_calls > 0) {
					ImGui::TextDisabled(
					    "Characters: %zu sweepCapsule, %zu overlapCapsule calls this tick (each samples the voxel walk "
					    "several times internally)",
					    phys.character_sweep_calls,
					    phys.character_overlap_calls
					);
				}
				ImGui::TextDisabled(
				    "Solve breakdown: cache %.2f  wake %.2f  prepare %.2f  islands %.2f  solve %.2f  sleep %.2f",
				    phys.cache_update_ms,
				    phys.wake_groups_ms,
				    phys.prepare_constraints_ms,
				    phys.build_islands_ms,
				    phys.island_solve_ms,
				    phys.sleep_update_ms
				);

				ImGui::Separator();
				ImGui::Text(
				    "Connectivity %zu jobs dispatched, %zu stale, %zu shapes still waiting",
				    phys.connectivity_jobs_dispatched,
				    phys.connectivity_jobs_stale,
				    phys.connectivity_shapes_waiting
				);
				if (phys.connectivity_shapes_waiting > 0) {
					ImGui::TextColored(
					    ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Connectivity backlog: destruction is outrunning the per tick job cap"
					);
				}
				ImGui::Text(
				    "Fragments %zu spawned this tick, %zu pending, %zu active (%zu sleep locked), %zu despawned",
				    phys.fragments_spawned,
				    phys.fragments_pending,
				    phys.fragments_active,
				    phys.fragments_sleep_locked,
				    phys.fragments_despawned
				);
				if (phys.fragment_spawn_failures > 0) {
					ImGui::TextColored(
					    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
					    "%zu fragment extractions failed this tick, brick pool is full",
					    phys.fragment_spawn_failures
					);
				}
				if (phys.static_splits_spawned > 0) {
					ImGui::Text("%zu static bodies split off this tick", phys.static_splits_spawned);
				}

				ImGui::Separator();
				const float pool_ratio = phys.brick_pool_capacity > 0 ? static_cast<float>(phys.brick_pool_allocated) /
				                                                            static_cast<float>(phys.brick_pool_capacity)
				                                                      : 0.0f;
				ImVec4 pool_color;
				if (pool_ratio > 0.9f) {
					pool_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
				} else if (pool_ratio > 0.75f) {
					pool_color = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
				} else {
					pool_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
				}
				ImGui::TextColored(
				    pool_color,
				    "Brick pool %u of %u bricks (%.0f%%)",
				    phys.brick_pool_allocated,
				    phys.brick_pool_capacity,
				    pool_ratio * 100.0f
				);

				ImGui::Separator();
				const size_t total_cached = phys.reused_cached_contacts + phys.cold_cached_contacts;
				const float warm_ratio =
				    total_cached > 0 ? static_cast<float>(phys.reused_cached_contacts) / static_cast<float>(total_cached) : 1.0f;
				ImGui::TextDisabled(
				    "Contacts %zu begin, %zu persist, %zu end, %.0f%% warm started (%zu of %zu)",
				    phys.contact_begins,
				    phys.contact_persists,
				    phys.contact_ends,
				    warm_ratio * 100.0f,
				    phys.reused_cached_contacts,
				    total_cached
				);
				ImGui::TextDisabled(
				    "Sleep %zu slept, %zu woken (%zu approach, %zu racing, %zu contact end, %zu support)",
				    phys.bodies_slept,
				    phys.bodies_woken,
				    phys.woken_by_approach,
				    phys.woken_by_racing,
				    phys.woken_by_contact_end,
				    phys.woken_by_support_loss
				);
				ImGui::TextDisabled(
				    "Constraints %zu warm started, %zu rejected, %zu invalid, %zu islands, %zu parallel batches",
				    phys.warm_started_constraints,
				    phys.rejected_constraints,
				    phys.invalid_constraints,
				    phys.island_jobs,
				    phys.max_constraint_batches
				);
				ImGui::TextDisabled(
				    "Narrowphase %zu jobs, %zu candidates, %zu collisions, %zu rejected manifolds, %zu sleeping pairs skipped",
				    phys.narrow_jobs,
				    phys.narrow_candidates,
				    phys.narrow_collisions,
				    phys.rejected_manifolds,
				    phys.sleeping_pairs_skipped
				);

				using PT = physics::NarrowPhasePairType;
				constexpr std::array<std::pair<PT, const char*>, 4> k_voxel_pairs {
				  {{PT::sphere_voxel, "Sphere-voxel"},
					 {PT::box_voxel, "Box-voxel"},
					 {PT::capsule_voxel, "Capsule-voxel"},
					 {PT::voxel_voxel, "Voxel-voxel"}}
				};
				if (ImGui::BeginTable("##voxel_pair_candidates", 3, ImGuiTableFlags_SizingFixedFit)) {
					for (const auto& [type, label] : k_voxel_pairs) {
						ImGui::TableNextColumn();
						ImGui::TextDisabled("%s", label);
						ImGui::TableNextColumn();
						ImGui::TextDisabled("%zu", phys.narrow_pair_candidates[static_cast<size_t>(type)]);
						ImGui::TableNextColumn();
						ImGui::TextDisabled("%.2f ms", phys.narrow_pair_time_ms[static_cast<size_t>(type)]);
					}
					ImGui::EndTable();
				}
				if (phys.box_voxel_split_pairs > 0 || phys.voxel_voxel_split_pairs > 0) {
					ImGui::TextDisabled(
					    "Split across jobs: %zu of %zu box-voxel, %zu of %zu voxel-voxel",
					    phys.box_voxel_split_pairs,
					    phys.narrow_pair_candidates[static_cast<size_t>(physics::NarrowPhasePairType::box_voxel)],
					    phys.voxel_voxel_split_pairs,
					    phys.narrow_pair_candidates[static_cast<size_t>(physics::NarrowPhasePairType::voxel_voxel)]
					);
				}
				ImGui::TextDisabled(
				    "Max estimated_voxels this tick: box-voxel %zu (longest axis %.2f m)  voxel-voxel %zu",
				    phys.box_voxel_max_estimated_voxels,
				    phys.box_voxel_max_extent_meters,
				    phys.voxel_voxel_max_estimated_voxels
				);
				if (phys.voxel_voxel_resolved_pairs > 0) {
					ImGui::TextDisabled(
					    "Voxel-voxel resolved %zu, of those %zu actually overlapped (rest: bounds don't overlap, "
					    "not a bug)",
					    phys.voxel_voxel_resolved_pairs,
					    phys.voxel_voxel_nondegenerate_pairs
					);
				}
				ImGui::TextColored(
				    ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
				    "Live thresholds: box-voxel %u  voxel-voxel %u  regions %u",
				    physics::tunables().box_voxel_split_min_voxels,
				    physics::tunables().voxel_voxel_split_min_voxels,
				    physics::tunables().voxel_pair_split_regions
				);
			}

			ImGui::Separator();
			if (voxel_debug::isView(frame->render_mode)) {
				ImGui::Text("Render mode: voxel view %u, legend bottom left", frame->render_mode);
			} else {
				ImGui::Text("Render mode: %s", frame->render_mode == 1 ? "Cluster Heatmap (toolbar Mode button)" : "Lit");
			}
			if (frame->render_mode == 1) {
				ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "Blue = 0 lights/cluster");
				ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Red = 8+ lights/cluster");
			}
		}
		if (m_editor_panels) {
			ImGui::End();
		}

		m_voxels->drawOverlay(*frame);

		if (frame->render_mode == 1 && m_cluster_lighting_pass != nullptr) {
			m_cluster_lighting_pass->requestGridReadback();

			const auto counts = m_cluster_lighting_pass->getClusterLightGridCounts(frame_index);
			if (!counts.empty()) {
				using namespace clustered_lighting;

				ImDrawList* draw_list = ImGui::GetForegroundDrawList();
				const float cell_w = frame->viewport_extent.x / static_cast<float>(k_cluster_dim_x);
				const float cell_h = frame->viewport_extent.y / static_cast<float>(k_cluster_dim_y);

				for (uint32_t ty = 0; ty < k_cluster_dim_y; ++ty) {
					for (uint32_t tx = 0; tx < k_cluster_dim_x; ++tx) {
						uint32_t max_count = 0;
						for (uint32_t tz = 0; tz < k_cluster_dim_z; ++tz) {
							const uint32_t idx = tx + (ty * k_cluster_dim_x) + (tz * k_cluster_dim_x * k_cluster_dim_y);
							max_count = std::max(max_count, counts[idx]);
						}

						const ImVec2 cell_min(static_cast<float>(tx) * cell_w, static_cast<float>(ty) * cell_h);
						const ImVec2 cell_max(cell_min.x + cell_w, cell_min.y + cell_h);

						const ImVec4 heat = clusterHeatmapColorImGui(max_count);
						draw_list->AddRectFilled(cell_min, cell_max, ImGui::ColorConvertFloat4ToU32(ImVec4(heat.x, heat.y, heat.z, 0.30f)));
						draw_list->AddRect(cell_min, cell_max, IM_COL32(255, 255, 255, 50));

						const std::string label = std::format("{}", max_count);
						const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
						const ImVec2 text_pos(cell_min.x + ((cell_w - text_size.x) * 0.5f), cell_min.y + ((cell_h - text_size.y) * 0.5f));
						draw_list->AddText(ImVec2(text_pos.x + 1, text_pos.y + 1), IM_COL32(0, 0, 0, 200), label.c_str());
						draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), label.c_str());
					}
				}
			}
		}

		ImGui::Render();
	}

	const auto& core = VulkanRenderer::instance->getCore();
	auto& fill_buffer = m_fill_vertex_buffers[frame_index];
	const auto& fill_vertices = frame->debug_triangle_vertices;
	m_fill_vertex_counts[frame_index] = static_cast<uint32_t>(fill_vertices.size());
	if (!fill_vertices.empty()) {
		ZoneScopedN("DebugPass::SortFill");

		const size_t triangle_count = fill_vertices.size() / 3;
		m_fill_sort_order.resize(triangle_count);
		m_fill_sort_depths.resize(triangle_count);

		const glm::mat4& view = frame->frame_data.view;
		for (size_t triangle = 0; triangle < triangle_count; ++triangle) {
			const size_t base = triangle * 3;
			const glm::vec3 center =
			    (fill_vertices[base].position + fill_vertices[base + 1].position + fill_vertices[base + 2].position) / 3.0f;
			m_fill_sort_order[triangle] = static_cast<uint32_t>(triangle);
			m_fill_sort_depths[triangle] = (view * glm::vec4(center, 1.0f)).z;
		}

		std::stable_sort(m_fill_sort_order.begin(), m_fill_sort_order.end(), [this](uint32_t a, uint32_t b) {
			return m_fill_sort_depths[a] < m_fill_sort_depths[b];
		});

		ensureLineCapacity(core, fill_buffer, fill_vertices.size());
		auto* destination = static_cast<debug::Vertex*>(fill_buffer.mapped);
		for (const uint32_t triangle : m_fill_sort_order) {
			std::memcpy(destination, &fill_vertices[static_cast<size_t>(triangle) * 3], 3 * sizeof(debug::Vertex));
			destination += 3;
		}
		fill_buffer.buffer.getAllocation().flush(0, fill_vertices.size() * sizeof(debug::Vertex));
	}
	auto& buffer = m_line_vertex_buffers[frame_index];
	const auto& vertices = frame->debug_line_vertices;

	m_line_vertex_counts[frame_index] = static_cast<uint32_t>(vertices.size());
	if (vertices.empty()) {
		return;
	}

	ensureLineCapacity(core, buffer, vertices.size());
	std::memcpy(buffer.mapped, vertices.data(), vertices.size() * sizeof(debug::Vertex));
	buffer.buffer.getAllocation().flush(0, vertices.size() * sizeof(debug::Vertex));
}

void DebugPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	ZoneScoped;
	(void)image_index;

	if (frame_index >= m_frame_descriptor_sets.size()) {
		TOAST_ERROR("Render", "Frame index {} out of bounds for descriptor sets", frame_index);
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	if (frame->probe_capture_index >= 0) {
		return;
	}

	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_frame_descriptor_sets[frame_index]},
	    {}
	);

	const uint32_t line_vertex_count = frame_index < m_line_vertex_counts.size() ? m_line_vertex_counts[frame_index] : 0;
	if (m_fill_vertex_counts[frame_index] > 0 && m_fill_pipeline.isReady()) {
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_fill_pipeline.getPipeline());
		const DrawPushConstants pc {glm::mat4(1.0f)};
		cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(DrawPushConstants), &pc);
		cmd.bindVertexBuffers(
		    0, std::array<vk::Buffer, 1> {*m_fill_vertex_buffers[frame_index].buffer}, std::array<vk::DeviceSize, 1> {0}
		);
		cmd.draw(m_fill_vertex_counts[frame_index], 1, 0, 0);
	}
	if (line_vertex_count > 0 && m_line_pipeline.isReady()) {
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_line_pipeline.getPipeline());

		const DrawPushConstants pc {glm::mat4(1.0f)};
		cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(DrawPushConstants), &pc);

		cmd.bindVertexBuffers(
		    0, std::array<vk::Buffer, 1> {*m_line_vertex_buffers[frame_index].buffer}, std::array<vk::DeviceSize, 1> {0}
		);
		cmd.draw(line_vertex_count, 1, 0, 0);
	}

	if (!frame->debug_meshes.empty() && m_mesh_pipeline.isReady()) {
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_mesh_pipeline.getPipeline());

		for (const auto& entry : frame->debug_meshes) {
			if (!entry.mesh.hasValue()) {
				continue;
			}

			const auto& gpu_mesh = entry.mesh->gpuMesh();
			if (!gpu_mesh.isReady() || gpu_mesh.getIndexCount() == 0) {
				continue;
			}

			DrawPushConstants pc {};
			pc.model = entry.model;
			pc.tint = entry.tint;
			cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(DrawPushConstants), &pc);

			gpu_mesh.bind(cmd);
			cmd.drawIndexed(gpu_mesh.getIndexCount(), 1, 0, 0, 0);
		}
	}

	if (!frame->debug_billboards.empty() && m_billboard_pipeline.isReady() && frame_index < m_billboard_frame_sets.size()) {
		const auto& core = VulkanRenderer::instance->getCore();
		const vk::PipelineLayout layout = *m_billboard_layout.getPipelineLayout();

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_billboard_pipeline.getPipeline());
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eGraphics, layout, 0, std::array<vk::DescriptorSet, 1> {*m_billboard_frame_sets[frame_index]}, {}
		);

		vk::DescriptorSet bound_texture_set {};
		for (const auto& billboard : frame->debug_billboards) {
			if (!billboard.texture.hasValue()) {
				continue;
			}
			const auto& gpu_texture = billboard.texture->gpuTexture();
			if (!gpu_texture.isReady() || !gpu_texture.getView()) {
				continue;
			}

			const vk::DescriptorSet texture_set = billboardTextureSet(core, gpu_texture.getView());
			if (!texture_set) {
				continue;
			}
			if (texture_set != bound_texture_set) {
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, std::array {texture_set}, {});
				bound_texture_set = texture_set;
			}

			BillboardPushConstants pc {};
			pc.center_size = glm::vec4(billboard.position, billboard.size);
			pc.tint = billboard.tint;
			cmd.pushConstants(layout, vk::ShaderStageFlagBits::eAll, 0, sizeof(BillboardPushConstants), &pc);

			cmd.draw(6, 1, 0, 0);
		}
	}

	if (!frame->debug_gizmo_instances.empty() && m_gizmo_pipeline.isReady() && m_gizmo_vertex_count > 0) {
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_gizmo_pipeline.getPipeline());
		cmd.bindVertexBuffers(0, std::array<vk::Buffer, 1> {*m_gizmo_vertex_buffer}, std::array<vk::DeviceSize, 1> {0});

		for (const auto& transform : frame->debug_gizmo_instances) {
			DrawPushConstants pc {};
			pc.model = transform;
			cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(DrawPushConstants), &pc);
			cmd.draw(m_gizmo_vertex_count, 1, 0, 0);
		}
	}

	if (frame->transform_gizmo.visible && m_gizmo_pipeline.isReady()) {
		vk::Buffer buffer;
		const std::array<GizmoHandleRange, 7>* handles = nullptr;
		switch (frame->transform_gizmo.tool) {
			case toast::GizmoTool::translate:
				buffer = *m_translate_gizmo_vertex_buffer;
				handles = &m_translate_gizmo_handles;
				break;
			case toast::GizmoTool::rotate:
				buffer = *m_rotate_gizmo_vertex_buffer;
				handles = &m_rotate_gizmo_handles;
				break;
			case toast::GizmoTool::scale:
				buffer = *m_scale_gizmo_vertex_buffer;
				handles = &m_scale_gizmo_handles;
				break;
			default: break;
		}

		if (handles != nullptr) {
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_gizmo_pipeline.getPipeline());
			cmd.bindVertexBuffers(0, std::array<vk::Buffer, 1> {buffer}, std::array<vk::DeviceSize, 1> {0});

			const glm::vec4 k_highlight {1.0f, 0.85f, 0.1f, 1.0f};

			for (size_t i = 0; i < handles->size(); ++i) {
				const auto& range = (*handles)[i];
				if (range.vertex_count == 0) {
					continue;
				}
				const auto handle = static_cast<toast::GizmoHandle>(i);
				const bool is_active = handle == frame->transform_gizmo.active;
				const bool highlighted = handle == frame->transform_gizmo.hover || is_active;

				DrawPushConstants pc {};
				pc.model = frame->transform_gizmo.model;

				if (frame->transform_gizmo.tool == toast::GizmoTool::scale && is_active) {
					glm::vec3 stretch {1.0f};
					if (handle == toast::GizmoHandle::center) {
						stretch = glm::vec3(frame->transform_gizmo.drag_scale_factor);
					} else {
						const auto axis_index = static_cast<int>(handle) - static_cast<int>(toast::GizmoHandle::axis_x);
						stretch[axis_index] = frame->transform_gizmo.drag_scale_factor;
					}
					pc.model = pc.model * glm::scale(glm::mat4(1.0f), stretch);
				}

				pc.tint = highlighted ? k_highlight : range.base_color;
				// Stage flags must cover every stage in the layout
				cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(DrawPushConstants), &pc);
				cmd.draw(range.vertex_count, 1, range.first_vertex, 0);
			}
		}
	}

	if (frame->transform_gizmo.size_handle_count > 0 && m_gizmo_pipeline.isReady() && m_size_gizmo_vertex_count > 0) {
		const glm::vec4 k_highlight {1.0f, 0.85f, 0.1f, 1.0f};
		const glm::vec4 k_size_dot_color {0.0f, 1.0f, 0.251f, 1.0f};    // editor green, matching the collider

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_gizmo_pipeline.getPipeline());
		cmd.bindVertexBuffers(0, std::array<vk::Buffer, 1> {*m_size_gizmo_vertex_buffer}, std::array<vk::DeviceSize, 1> {0});

		for (uint32_t i = 0; i < frame->transform_gizmo.size_handle_count; ++i) {
			const auto& dot = frame->transform_gizmo.size_handles[i];
			const bool highlighted = dot.handle == frame->transform_gizmo.hover || dot.handle == frame->transform_gizmo.active;

			DrawPushConstants pc {};
			pc.model = glm::translate(glm::mat4(1.0f), dot.world_position) *
			           glm::scale(glm::mat4(1.0f), glm::vec3(frame->transform_gizmo.size_handle_scale));
			pc.tint = highlighted ? k_highlight : k_size_dot_color;

			cmd.pushConstants(*m_shader_layout.getPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, sizeof(DrawPushConstants), &pc);
			cmd.draw(m_size_gizmo_vertex_count, 1, 0, 0);
		}
	}

	if (m_imgui_ready) {
		ImDrawData* draw_data = ImGui::GetDrawData();
		if (draw_data != nullptr) {
			ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
		}
	}
}

void DebugPass::createResources(const renderer::VulkanCore& core) {
	ZoneScoped;
	const auto& device = core.getDevice();
	const auto& layouts = m_shader_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_CRITICAL("Render", "ShaderLayout has no descriptor set layouts");
		return;
	}

	const vk::DescriptorSetLayout frame_set_layout = *layouts[0];
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	m_frame_descriptor_sets.clear();
	m_frame_descriptor_sets.reserve(VulkanRenderer::k_frames_in_flight);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &frame_set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_frame_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_frame_descriptor_sets[i], std::format("DebugPass FrameSet[{}]", i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			TOAST_CRITICAL("Render", "Frame UBO buffer missing for frame {}", i);
			continue;
		}

		const vk::DescriptorBufferInfo buffer_info(**frame_res->gpu_buffer, 0, sizeof(VulkanRenderer::FrameUBO));
		const vk::WriteDescriptorSet write(
		    *m_frame_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info
		);
		device.updateDescriptorSets(write, {});
	}

	m_line_vertex_buffers.resize(VulkanRenderer::k_frames_in_flight);
	m_line_vertex_counts.assign(VulkanRenderer::k_frames_in_flight, 0);
	m_fill_vertex_buffers.resize(VulkanRenderer::k_frames_in_flight);
	m_fill_vertex_counts.assign(VulkanRenderer::k_frames_in_flight, 0);

	createGizmoGeometry(core);

	createTranslateGizmoGeometry(core);
	createRotateGizmoGeometry(core);
	createScaleGizmoGeometry(core);
	createSizeGizmoGeometry(core);
}

void DebugPass::createBillboardResources(
    const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent
) {
	ZoneScoped;
	const auto shader = acquireShader("core://shaders/debug_billboard.slang");
	if (!shader) {
		TOAST_ERROR("Render", "DebugPass billboard shader unavailable, debug billboards will not draw");
		return;
	}

	m_billboard_layout.rebuild(core, shader->reflection, "DebugPass Billboard");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "DebugPass Billboard";
	config.color_format = color_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_billboard_layout.getPipelineLayout();
	config.topology = vk::PrimitiveTopology::eTriangleList;
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_test = true;
	config.depth_write = false;
	config.blend_preset = VulkanPipeline::BlendPreset::alpha;
	m_billboard_pipeline.rebuild(core, config);

	const auto& device = core.getDevice();

	const auto sampler_ci = linearClampMippedSamplerInfo(VK_LOD_CLAMP_NONE);
	m_billboard_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(core, *m_billboard_sampler, "DebugPass BillboardSampler");

	const auto& layouts = m_billboard_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_ERROR("Render", "DebugPass billboard layout has no descriptor sets");
		return;
	}

	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();
	const vk::DescriptorSetLayout frame_set_layout = *layouts[0];

	m_billboard_frame_sets.clear();
	m_billboard_frame_sets.reserve(VulkanRenderer::k_frames_in_flight);
	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &frame_set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_billboard_frame_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_billboard_frame_sets[i], std::format("DebugPass BillboardFrameSet[{}]", i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			continue;
		}
		const vk::DescriptorBufferInfo buffer_info(**frame_res->gpu_buffer, 0, sizeof(VulkanRenderer::FrameUBO));
		const vk::WriteDescriptorSet write(
		    *m_billboard_frame_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info
		);
		device.updateDescriptorSets(write, {});
	}
}

auto DebugPass::billboardTextureSet(const renderer::VulkanCore& core, vk::ImageView view) -> vk::DescriptorSet {
	if (const auto it = m_billboard_texture_sets.find(view); it != m_billboard_texture_sets.end()) {
		return *it->second;
	}

	const auto& layouts = m_billboard_layout.getDescriptorSetLayouts();
	if (layouts.size() < 2) {
		return nullptr;
	}

	const auto& device = core.getDevice();
	const vk::DescriptorSetLayout texture_set_layout = *layouts[1];
	const vk::DescriptorSetAllocateInfo alloc_info(VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &texture_set_layout);
	auto allocated = device.allocateDescriptorSets(alloc_info);

	vk::DescriptorImageInfo image_info {};
	image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	image_info.imageView = view;
	image_info.sampler = *m_billboard_sampler;

	const vk::WriteDescriptorSet write(*allocated[0], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info);
	device.updateDescriptorSets(write, {});

	auto [it, _] = m_billboard_texture_sets.emplace(view, std::move(allocated[0]));
	return *it->second;
}

void DebugPass::initImGui(const renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format) {
	ZoneScoped;
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.BackendPlatformName = "toast_engine (manual input feed)";
	io.BackendRendererName = "imgui_impl_vulkan";

	// FIXME no OS cursor or clipboard integration yet
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.SetClipboardTextFn = nullptr;
	io.GetClipboardTextFn = nullptr;

	const std::array<vk::Format, 1> color_formats {color_format};
	vk::PipelineRenderingCreateInfo rendering_ci {};
	rendering_ci.colorAttachmentCount = 1;
	rendering_ci.pColorAttachmentFormats = color_formats.data();
	// Must match the depth attachment of the shared scope
	rendering_ci.depthAttachmentFormat = depth_format;

	ImGui_ImplVulkan_InitInfo init_info {};
	init_info.ApiVersion = VK_API_VERSION_1_4;
	init_info.Instance = *core.getInstance();
	init_info.PhysicalDevice = *core.getPhysicalDevice();
	init_info.Device = *core.getDevice();
	init_info.QueueFamily = core.getGraphicsQueueFamilyIndex();
	init_info.Queue = core.getGraphicsQueue();
	init_info.DescriptorPoolSize = 8;
	init_info.MinImageCount = 2;
	init_info.ImageCount = VulkanRenderer::k_frames_in_flight;
	init_info.UseDynamicRendering = true;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(rendering_ci);

	m_imgui_ready = ImGui_ImplVulkan_Init(&init_info);
	if (!m_imgui_ready) {
		TOAST_ERROR("DebugPass", "Failed to initialize ImGui Vulkan backend");
		ImGui::DestroyContext();
	}
}

void DebugPass::createGizmoGeometry(const renderer::VulkanCore& core) {
	ZoneScoped;
	constexpr float k_shaft_length = 0.8f;
	constexpr float k_shaft_half_size = 0.02f;
	constexpr float k_head_length = 0.25f;
	constexpr float k_head_half_size = 0.06f;

	const glm::vec4 k_red {1.0f, 0.1f, 0.1f, 1.0f};
	const glm::vec4 k_green {0.1f, 1.0f, 0.1f, 1.0f};
	const glm::vec4 k_blue {0.1f, 0.1f, 1.0f, 1.0f};

	std::vector<debug::Vertex> vertices;

	for (const auto& [axis, color] : {
	       std::pair {0,   k_red},
          std::pair {1, k_green},
          std::pair {2,  k_blue}
	}) {
		appendShaftAlongAxis(vertices, axis, k_shaft_length, k_shaft_half_size, color);
		appendPyramidAlongAxis(vertices, axis, k_shaft_length, k_shaft_length + k_head_length, k_head_half_size, color);
	}

	m_gizmo_vertex_count = static_cast<uint32_t>(vertices.size());

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = vertices.size() * sizeof(debug::Vertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_gizmo_vertex_buffer, "DebugPass GizmoVertexBuffer");

	void* mapped = m_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(debug::Vertex));
	m_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(debug::Vertex));
}

void DebugPass::createTranslateGizmoGeometry(const renderer::VulkanCore& core) {
	using namespace toast::gizmo_layout;

	const glm::vec4 k_white {1.0f, 1.0f, 1.0f, 1.0f};

	std::vector<debug::Vertex> vertices;

	auto record_handle = [&](toast::GizmoHandle handle, size_t start, glm::vec4 base_color) {
		m_translate_gizmo_handles[static_cast<size_t>(handle)] = {
		  static_cast<uint32_t>(start), static_cast<uint32_t>(vertices.size() - start), base_color
		};
	};

	for (int axis = 0; axis < 3; ++axis) {
		const size_t start = vertices.size();
		appendShaftAlongAxis(vertices, axis, k_shaft_length, k_shaft_half_size, k_white);
		appendPyramidAlongAxis(vertices, axis, k_shaft_length, k_shaft_length + k_head_length, k_head_half_size, k_white);
		record_handle(k_axis_handles[axis], start, k_axis_colors[axis]);
	}

	constexpr std::array<toast::GizmoHandle, 3> plane_handles {
	  toast::GizmoHandle::plane_xy, toast::GizmoHandle::plane_yz, toast::GizmoHandle::plane_xz
	};
	constexpr std::array<int, 3> plane_normal_axis {2, 0, 1};    // xy Z yz X xz Y
	const std::array<glm::vec4, 3> plane_colors {
	  glm::vec4 {0.161f, 0.678f,   1.0f, 1.0f},
     glm::vec4 {  1.0f, 0.086f, 0.349f, 1.0f},
     glm::vec4 {  0.0f,   1.0f, 0.251f, 1.0f}
	};
	for (int i = 0; i < 3; ++i) {
		const size_t start = vertices.size();
		appendQuad(vertices, plane_normal_axis[i], k_plane_offset, k_plane_size, k_white);
		record_handle(plane_handles[i], start, plane_colors[i]);
	}

	{
		const size_t start = vertices.size();
		appendBox(vertices, glm::vec3(-k_center_half_size), glm::vec3(k_center_half_size), k_white);
		record_handle(toast::GizmoHandle::center, start, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	}

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = vertices.size() * sizeof(debug::Vertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_translate_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_translate_gizmo_vertex_buffer, "DebugPass TranslateGizmoVertexBuffer");

	void* mapped = m_translate_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(debug::Vertex));
	m_translate_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(debug::Vertex));
}

void DebugPass::createRotateGizmoGeometry(const renderer::VulkanCore& core) {
	using namespace toast::gizmo_layout;
	const glm::vec4 k_white {1.0f, 1.0f, 1.0f, 1.0f};

	std::vector<debug::Vertex> vertices;

	for (int axis = 0; axis < 3; ++axis) {
		const size_t start = vertices.size();
		appendRing(vertices, axis, k_ring_radius, k_ring_thickness, k_ring_segments, k_white);
		m_rotate_gizmo_handles[static_cast<size_t>(k_axis_handles[axis])] = {
		  static_cast<uint32_t>(start), static_cast<uint32_t>(vertices.size() - start), k_axis_colors[axis]
		};
	}

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = vertices.size() * sizeof(debug::Vertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_rotate_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_rotate_gizmo_vertex_buffer, "DebugPass RotateGizmoVertexBuffer");

	void* mapped = m_rotate_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(debug::Vertex));
	m_rotate_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(debug::Vertex));
}

void DebugPass::createScaleGizmoGeometry(const renderer::VulkanCore& core) {
	using namespace toast::gizmo_layout;
	const glm::vec4 k_white {1.0f, 1.0f, 1.0f, 1.0f};

	std::vector<debug::Vertex> vertices;

	for (int axis = 0; axis < 3; ++axis) {
		const size_t start = vertices.size();
		appendShaftAlongAxis(vertices, axis, k_shaft_length, k_shaft_half_size, k_white);

		glm::vec3 min(-k_scale_head_half_size);
		glm::vec3 max(k_scale_head_half_size);
		min[axis] = k_shaft_length;
		max[axis] = k_shaft_length + (2.0f * k_scale_head_half_size);
		appendBox(vertices, min, max, k_white);

		m_scale_gizmo_handles[static_cast<size_t>(k_axis_handles[axis])] = {
		  static_cast<uint32_t>(start), static_cast<uint32_t>(vertices.size() - start), k_axis_colors[axis]
		};
	}

	{
		const size_t start = vertices.size();
		appendBox(vertices, glm::vec3(-k_center_half_size), glm::vec3(k_center_half_size), k_white);
		m_scale_gizmo_handles[static_cast<size_t>(toast::GizmoHandle::center)] = {
		  static_cast<uint32_t>(start), static_cast<uint32_t>(vertices.size() - start), glm::vec4(0.9f, 0.9f, 0.9f, 1.0f)
		};
	}

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = vertices.size() * sizeof(debug::Vertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_scale_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_scale_gizmo_vertex_buffer, "DebugPass ScaleGizmoVertexBuffer");

	void* mapped = m_scale_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(debug::Vertex));
	m_scale_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(debug::Vertex));
}

void DebugPass::createSizeGizmoGeometry(const renderer::VulkanCore& core) {
	using namespace toast::gizmo_layout;
	const glm::vec4 k_white {1.0f, 1.0f, 1.0f, 1.0f};

	std::vector<debug::Vertex> vertices;
	appendSphere(vertices, k_size_dot_half_size * 1.25f, 8, 12, k_white);
	m_size_gizmo_vertex_count = static_cast<uint32_t>(vertices.size());

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = vertices.size() * sizeof(debug::Vertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_size_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_size_gizmo_vertex_buffer, "DebugPass SizeGizmoVertexBuffer");

	void* mapped = m_size_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(debug::Vertex));
	m_size_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(debug::Vertex));
}

void DebugPass::ensureLineCapacity(const renderer::VulkanCore& core, DynamicVertexBuffer& buffer, size_t required_vertex_count) {
	const vk::DeviceSize required_bytes = required_vertex_count * sizeof(debug::Vertex);
	if (required_bytes <= buffer.capacity_bytes) {
		return;
	}

	const vk::DeviceSize new_capacity = std::max<vk::DeviceSize>(required_bytes * 2, sizeof(debug::Vertex) * 1024);

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = new_capacity;
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	buffer.buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	buffer.capacity_bytes = new_capacity;
	buffer.mapped = buffer.buffer.getAllocation().getInfo().pMappedData;
	setDebugName(core, *buffer.buffer, "DebugPass LineVertexBuffer");
}

}
