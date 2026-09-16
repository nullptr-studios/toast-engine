/// @file debug_pass.cpp
/// @author dario
/// @date 10/06/2026

#include "debug_pass.hpp"

#include "../clustered_lighting_constants.hpp"
#include "../ray_tracing_scene.hpp"
#include "../shader_cache.hpp"
#include "../skinned_blas_pool.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_mesh.hpp"
#include "../vulkan_renderer.hpp"
#include "../vulkan_texture.hpp"
#include "cluster_lighting_pass.hpp"
#include "environment_pass.hpp"
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
#include <tracy/Tracy.hpp>

namespace renderer {

struct DebugPass::PerfOverlay {
	static constexpr size_t k_history = 240;

	std::chrono::steady_clock::time_point last_sample {};
	VulkanRenderer::PerfCounters last {};
	uint32_t last_skipped = 0;
	bool has_sample = false;

	float render_fps = 0.0f;
	float game_fps = 0.0f;
	float tick_rate = 0.0f;
	float skipped_per_second = 0.0f;
	float repeated_ratio = 0.0f;
	float wait_ratio = 0.0f;

	float draw_ms = 0.0f;
	float upload_ms = 0.0f;
	float gpu_wait_ms = 0.0f;
	float acquire_ms = 0.0f;
	float record_ms = 0.0f;
	float submit_ms = 0.0f;
	float present_ms = 0.0f;
	float frame_wait_ms = 0.0f;
	float pacing_ms = 0.0f;

	float build_ms = 0.0f;
	float slot_wait_ms = 0.0f;

	std::array<float, k_history> interval_history {};
	std::array<float, k_history> gpu_history {};
	size_t history_cursor = 0;
	size_t history_filled = 0;
	float p99_interval_ms = 0.0f;
};

namespace {

constexpr std::array<toast::GizmoHandle, 3> k_axis_handles {
  toast::GizmoHandle::axis_x, toast::GizmoHandle::axis_y, toast::GizmoHandle::axis_z
};

constexpr std::array<glm::vec4, 3> k_axis_colors {
  glm::vec4 {  1.0f, 0.086f, 0.349f, 1.0f}, // X
  glm::vec4 {  0.0f,   1.0f, 0.251f, 1.0f}, // Y
  glm::vec4 {0.161f, 0.678f,   1.0f, 1.0f}  // Z
};

using DebugVertex = VulkanRenderer::DebugVertex;

void appendBox(std::vector<DebugVertex>& out, glm::vec3 min, glm::vec3 max, glm::vec4 color) {
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

void appendShaftAlongAxis(std::vector<DebugVertex>& out, int axis, float length, float half_size, glm::vec4 color) {
	glm::vec3 min {-half_size, -half_size, -half_size};
	glm::vec3 max {half_size, half_size, half_size};
	min[axis] = 0.0f;
	max[axis] = length;
	appendBox(out, min, max, color);
}

void appendPyramidAlongAxis(
    std::vector<DebugVertex>& out, int axis, float base_pos, float apex_pos, float half_size, glm::vec4 color
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

void appendQuad(std::vector<DebugVertex>& out, int axis, float offset, float size, glm::vec4 color) {
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

void appendRing(std::vector<DebugVertex>& out, int axis, float radius, float thickness, int segments, glm::vec4 color) {
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

	const vk::VertexInputBindingDescription debug_vertex_binding(0, sizeof(DebugVertex), vk::VertexInputRate::eVertex);
	const std::vector<vk::VertexInputAttributeDescription> debug_vertex_attributes {
	  vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(DebugVertex, position)),
	  vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(DebugVertex, color)),
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

void DebugPass::drawPerformanceWindow() {
	ZoneScoped;
	if (!m_perf) {
		m_perf = std::make_unique<PerfOverlay>();
	}
	PerfOverlay& perf = *m_perf;

	auto* renderer = VulkanRenderer::instance;
	const VulkanRenderer::PerfCounters counters = renderer->perfCounters();
	const uint32_t skipped = renderer->getSkippedBuildCount();
	const GpuTimer* gpu_timer = renderer->gpuTimer();
	const GpuTimer::Timings* gpu = gpu_timer != nullptr && gpu_timer->smoothed().valid ? &gpu_timer->smoothed() : nullptr;
	const GpuTimer::Timings* gpu_last = gpu_timer != nullptr && gpu_timer->last().valid ? &gpu_timer->last() : nullptr;
	const auto now = std::chrono::steady_clock::now();

	const float frame_interval_ms = ImGui::GetIO().DeltaTime * 1000.0f;
	perf.interval_history[perf.history_cursor] = frame_interval_ms;
	perf.gpu_history[perf.history_cursor] =
	    gpu_last != nullptr ? static_cast<float>(gpu_last->graphics_ms + gpu_last->compute_ms) : 0.0f;
	perf.history_cursor = (perf.history_cursor + 1) % PerfOverlay::k_history;
	perf.history_filled = std::min(perf.history_filled + 1, PerfOverlay::k_history);

	constexpr auto k_sample_interval = std::chrono::milliseconds(500);
	const bool first_sample = perf.last_sample.time_since_epoch().count() == 0;
	const bool sample_due = !first_sample && now - perf.last_sample >= k_sample_interval;

	if (sample_due) {
		const double seconds = std::chrono::duration<double>(now - perf.last_sample).count();
		const VulkanRenderer::PerfCounters& last = perf.last;
		const auto draws = static_cast<double>(counters.draws - last.draws);
		const auto built = static_cast<double>(counters.frames_built - last.frames_built);
		const auto ticks = static_cast<double>(counters.ticks - last.ticks);

		const auto per = [](uint64_t current, uint64_t previous, double count) {
			return count > 0.0 ? static_cast<float>(static_cast<double>(current - previous) / count / 1.0e6) : 0.0f;
		};

		perf.render_fps = static_cast<float>(draws / seconds);
		perf.game_fps = static_cast<float>(built / seconds);
		perf.tick_rate = static_cast<float>(ticks / seconds);
		perf.skipped_per_second = static_cast<float>(static_cast<double>(skipped - perf.last_skipped) / seconds);
		perf.repeated_ratio =
		    draws > 0.0 ? static_cast<float>(static_cast<double>(counters.repeated_draws - last.repeated_draws) / draws) : 0.0f;
		perf.wait_ratio =
		    std::min(1.0f, static_cast<float>(static_cast<double>(counters.slot_wait_ns - last.slot_wait_ns) / 1.0e9 / seconds));

		perf.draw_ms = per(counters.draw_work_ns, last.draw_work_ns, draws);
		perf.upload_ms = per(counters.upload_ns, last.upload_ns, draws);
		perf.gpu_wait_ms = per(counters.gpu_wait_ns, last.gpu_wait_ns, draws);
		perf.acquire_ms = per(counters.acquire_ns, last.acquire_ns, draws);
		perf.record_ms = per(counters.record_ns, last.record_ns, draws);
		perf.submit_ms = per(counters.submit_ns, last.submit_ns, draws);
		perf.present_ms = per(counters.present_ns, last.present_ns, draws);
		perf.frame_wait_ms = per(counters.frame_wait_ns, last.frame_wait_ns, draws);
		perf.pacing_ms = per(counters.pacing_ns, last.pacing_ns, draws);
		perf.build_ms = per(counters.build_ns, last.build_ns, built);
		perf.slot_wait_ms = per(counters.slot_wait_ns, last.slot_wait_ns, ticks);

		std::array<float, PerfOverlay::k_history> sorted = perf.interval_history;
		const size_t filled = perf.history_filled;
		std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(filled));
		perf.p99_interval_ms = filled > 0 ? sorted[std::min(filled - 1, filled * 99 / 100)] : 0.0f;

		perf.has_sample = true;
	}

	if (first_sample || sample_due) {
		perf.last_sample = now;
		perf.last = counters;
		perf.last_skipped = skipped;
	}

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, 12.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.85f);
	constexpr ImGuiWindowFlags k_flags =
	    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

	const bool open = ImGui::Begin("Performance", nullptr, k_flags);
	if (!open || !perf.has_sample) {
		if (open) {
			ImGui::TextDisabled("Sampling...");
		}
		ImGui::End();
		return;
	}

	const ImVec4 k_bad(1.0f, 0.35f, 0.35f, 1.0f);
	const ImVec4 k_warn(1.0f, 0.7f, 0.2f, 1.0f);
	const ImVec4 k_good(0.4f, 0.9f, 0.4f, 1.0f);
	constexpr float k_trend = 0.25f;

	float avg_ms = 0.0f;
	float min_ms = 0.0f;
	float max_ms = 0.0f;
	renderer->getFrameTimeStats(avg_ms, min_ms, max_ms);
	const auto cap = static_cast<float>(renderer->effectiveFrameRateLimit());
	const float interval_ms = perf.render_fps > 0.0f ? 1000.0f / perf.render_fps : 0.0f;
	const float gpu_ms = gpu != nullptr ? static_cast<float>(gpu->graphics_ms + gpu->compute_ms) : 0.0f;
	const auto share = [interval_ms](float ms) { return interval_ms > 0.0f ? ms / interval_ms : 0.0f; };

	const auto heaviest = [gpu](bool by_gpu) -> const GpuTimer::ScopeTiming* {
		if (gpu == nullptr) {
			return nullptr;
		}
		const GpuTimer::ScopeTiming* best = nullptr;
		for (size_t i = 0; i < gpu->scopes.size(); ++i) {
			const auto& scope = gpu->scopes[i];
			if (i + 1 < gpu->scopes.size() && gpu->scopes[i + 1].depth > scope.depth) {
				continue;
			}
			const double value = by_gpu ? scope.gpu_ms : scope.cpu_ms;
			if (best == nullptr || value > (by_gpu ? best->gpu_ms : best->cpu_ms)) {
				best = &scope;
			}
		}
		return best;
	};

	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);

	ImGui::Text("Renderer %6.1f fps  %6.2f ms", perf.render_fps, interval_ms);
	ImGui::TextDisabled("         avg %.2f  min %.2f  max %.2f  p99 %.2f ms", avg_ms, min_ms, max_ms, perf.p99_interval_ms);
	ImGui::Text("Game     %6.1f fps  %6.0f ticks/s", perf.game_fps, perf.tick_rate);
	if (gpu != nullptr) {
		ImGui::Text("GPU      %6.2f ms  %3.0f%%", gpu_ms, std::min(share(gpu_ms), 1.0f) * 100.0f);
	} else {
		ImGui::TextDisabled("GPU      n/a");
	}
	if (cap > 0.0f) {
		ImGui::TextDisabled("Cap      %6.0f fps%s", cap, renderer->applicationFocused() ? "" : "  unfocused");
	} else {
		ImGui::TextDisabled("Cap      off");
	}

	const float graph_width = ImGui::GetFontSize() * 28.0f;
	const float graph_max = std::max({perf.p99_interval_ms * 1.25f, interval_ms * 1.5f, gpu_ms * 1.5f, 1.0f});
	const auto history_count = static_cast<int>(perf.history_filled);
	const int history_offset = perf.history_filled < PerfOverlay::k_history ? 0 : static_cast<int>(perf.history_cursor);
	const std::string interval_label = std::format("frame ms (max {:.1f})", graph_max);
	ImGui::PlotLines(
	    "##frame_interval",
	    perf.interval_history.data(),
	    history_count,
	    history_offset,
	    interval_label.c_str(),
	    0.0f,
	    graph_max,
	    ImVec2(graph_width, 50.0f)
	);
	if (gpu != nullptr) {
		ImGui::PlotLines(
		    "##gpu_time",
		    perf.gpu_history.data(),
		    history_count,
		    history_offset,
		    "GPU ms",
		    0.0f,
		    graph_max,
		    ImVec2(graph_width, 36.0f)
		);
	}

	ImGui::Separator();

	const bool at_cap = cap > 0.0f && perf.render_fps >= cap * 0.95f;
	ImGui::TextDisabled("Game  ");
	ImGui::SameLine();
	if (perf.skipped_per_second > 0.0f || (!at_cap && perf.wait_ratio >= k_trend)) {
		ImGui::TextColored(k_bad, "Renderer exhausted");
		ImGui::SameLine();
		ImGui::TextDisabled("wait %.0f%%  skipped %.1f/s", perf.wait_ratio * 100.0f, perf.skipped_per_second);
	} else if (perf.repeated_ratio >= k_trend) {
		ImGui::TextColored(k_warn, "CPU starved");
		ImGui::SameLine();
		ImGui::TextDisabled("repeated %.0f%%", perf.repeated_ratio * 100.0f);
	} else if (at_cap) {
		ImGui::TextColored(k_good, "Capped");
	} else {
		ImGui::TextColored(k_good, "Balanced");
	}

	const float cpu_work_ms = perf.upload_ms + perf.record_ms + perf.submit_ms;
	const float display_ms = perf.acquire_ms + perf.present_ms;
	const float idle_ms = perf.frame_wait_ms + perf.pacing_ms;

	ImGui::TextDisabled("Render");
	ImGui::SameLine();
	if (share(perf.gpu_wait_ms) >= k_trend || (gpu != nullptr && share(gpu_ms) >= 0.85f && share(idle_ms) < k_trend)) {
		ImGui::TextColored(k_bad, "GPU bound");
		ImGui::SameLine();
		ImGui::TextDisabled("wait %.0f%%  gpu %.2f ms", share(perf.gpu_wait_ms) * 100.0f, gpu_ms);
		if (const auto* scope = heaviest(true); scope != nullptr) {
			const std::string_view name = scope->name.view();
			ImGui::TextDisabled("top    %.*s %.2f ms", static_cast<int>(name.size()), name.data(), scope->gpu_ms);
		}
	} else if (share(display_ms) >= k_trend) {
		ImGui::TextColored(k_warn, "Display paced");
		ImGui::SameLine();
		ImGui::TextDisabled("acquire+present %.0f%%", share(display_ms) * 100.0f);
	} else if (share(cpu_work_ms) >= 0.6f) {
		ImGui::TextColored(k_bad, "CPU bound");
		ImGui::SameLine();
		ImGui::TextDisabled("cpu %.2f ms  gpu %.2f ms", cpu_work_ms, gpu_ms);
		if (const auto* scope = heaviest(false); scope != nullptr) {
			const std::string_view name = scope->name.view();
			ImGui::TextDisabled("top    %.*s %.2f ms", static_cast<int>(name.size()), name.data(), scope->cpu_ms);
		}
	} else if (share(idle_ms) >= k_trend) {
		ImGui::TextColored(k_good, "Headroom");
		ImGui::SameLine();
		ImGui::TextDisabled("idle %.0f%%", share(idle_ms) * 100.0f);
	} else {
		ImGui::TextColored(k_good, "Balanced");
	}

	if (ImGui::CollapsingHeader("Render thread", ImGuiTreeNodeFlags_DefaultOpen)) {
		struct Phase {
			const char* label;
			float ms;
		};

		const std::array phases {
		  Phase { "GPU wait",   perf.gpu_wait_ms},
		  Phase {   "Record",     perf.record_ms},
		  Phase {   "Submit",     perf.submit_ms},
		  Phase {  "Uploads",     perf.upload_ms},
		  Phase {  "Acquire",    perf.acquire_ms},
		  Phase {  "Present",    perf.present_ms},
		  Phase {"Game wait", perf.frame_wait_ms},
		  Phase {"Cap sleep",     perf.pacing_ms},
		};

		float accounted_ms = 0.0f;
		if (ImGui::BeginTable("##render_phases", 3, ImGuiTableFlags_SizingFixedFit)) {
			for (const Phase& phase : phases) {
				accounted_ms += phase.ms;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(phase.label);
				ImGui::TableNextColumn();
				ImGui::Text("%6.2f ms", phase.ms);
				ImGui::TableNextColumn();
				const float fraction = std::clamp(share(phase.ms), 0.0f, 1.0f);
				ImGui::ProgressBar(
				    fraction, ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f), std::format("{:.0f}%", fraction * 100.0f).c_str()
				);
			}
			ImGui::EndTable();
		}
		ImGui::TextDisabled("Other     %6.2f ms", std::max(0.0f, interval_ms - accounted_ms));
	}

	if (ImGui::CollapsingHeader("GPU passes", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (gpu_timer == nullptr || !gpu_timer->supported()) {
			ImGui::TextDisabled("No timestamp queries");
		} else if (gpu == nullptr) {
			ImGui::TextDisabled("No data yet");
		} else {
			ImGui::Text("Graphics %.2f ms", gpu->graphics_ms);
			if (gpu_timer->computeSupported()) {
				ImGui::SameLine();
				ImGui::Text("  Compute %.2f ms", gpu->compute_ms);
			}

			constexpr ImGuiTableFlags k_table_flags =
			    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
			if (ImGui::BeginTable("##gpu_scopes", 4, k_table_flags)) {
				ImGui::TableSetupColumn("Pass");
				ImGui::TableSetupColumn("CPU");
				ImGui::TableSetupColumn("GPU");
				ImGui::TableSetupColumn("Runs");
				ImGui::TableHeadersRow();
				for (const auto& scope : gpu->scopes) {
					const std::string_view name = scope.name.view();
					const auto indent = static_cast<int>(scope.depth * 2);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (scope.cpu_ms < 0.01 && scope.gpu_ms < 0.01) {
						ImGui::TextDisabled("%*s%.*s", indent, "", static_cast<int>(name.size()), name.data());
					} else {
						ImGui::Text("%*s%.*s", indent, "", static_cast<int>(name.size()), name.data());
					}
					ImGui::TableNextColumn();
					ImGui::Text("%6.2f ms", scope.cpu_ms);
					ImGui::TableNextColumn();
					ImGui::Text("%6.2f ms", scope.gpu_ms);
					ImGui::TableNextColumn();
					ImGui::Text("%u", scope.count);
				}
				ImGui::EndTable();
			}
		}
	}

	if (ImGui::CollapsingHeader("Game thread")) {
		ImGui::Text("Ticks       %6.0f /s", perf.tick_rate);
		ImGui::Text("Built       %6.1f /s  %6.2f ms", perf.game_fps, perf.build_ms);
		ImGui::Text("Slot wait   %6.2f ms  %3.0f%%", perf.slot_wait_ms, perf.wait_ratio * 100.0f);
		ImGui::Text("Repeated    %6.0f%%", perf.repeated_ratio * 100.0f);
		ImGui::Text("Skipped     %6.1f /s", perf.skipped_per_second);
	}

	if (ImGui::CollapsingHeader("Scene")) {
		if (const auto* frame = renderer->renderingFrame(); frame != nullptr) {
			const auto visible = std::ranges::count_if(frame->mesh_instances, [](const auto& proxy) { return proxy.visible; });
			ImGui::Text("Mesh instances  %zu visible of %zu", static_cast<size_t>(visible), frame->mesh_instances.size());
			ImGui::Text("Material ranges %zu", frame->material_ranges.size());
			ImGui::Text("Lights          %zu", frame->lights.size());
			ImGui::Text("Voxel volumes   %zu", frame->voxel_instances.size());
		}
	}

	ImGui::PopTextWrapPos();
	ImGui::End();
}

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

		drawPerformanceWindow();

		if (m_editor_panels && ImGui::Begin("Toast Debug")) {
			{
				const uint32_t dropped = VulkanRenderer::instance->getDroppedFrameCount();
				const uint32_t out_of_order = VulkanRenderer::instance->getOutOfOrderFrameCount();
				if (out_of_order > 0) {
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Frames out of order: %u", out_of_order);
				}

				ImGui::TextDisabled("Frames dropped: %u", dropped);

				float cap = static_cast<float>(VulkanRenderer::instance->frameRateLimit());
				if (ImGui::SliderFloat("FPS cap", &cap, 0.0f, 144.0f, cap <= 0.0f ? "uncapped" : "%.0f")) {
					VulkanRenderer::instance->setFrameRateLimit(static_cast<double>(cap));
				}
			}

			ImGui::TextDisabled("Depth prepass: %u instances", VulkanRenderer::instance->getPrepassDrawnCount());

			if (const auto* shadows = VulkanRenderer::instance->getShadowPass(); shadows != nullptr) {
				ImGui::TextDisabled(
				    "Shadow pass: %u draws, %u scopes, %u cached",
				    shadows->getDrawCount(),
				    shadows->getPassCount(),
				    shadows->getCachedCount()
				);
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

			ImGui::Separator();
			ImGui::Text("Render mode: %s", frame->render_mode == 1 ? "Cluster Heatmap (toolbar Mode button)" : "Lit");
			if (frame->render_mode == 1) {
				ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "Blue = 0 lights/cluster");
				ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Red = 8+ lights/cluster");
			}
		}
		if (m_editor_panels) {
			ImGui::End();
		}

		if (frame->render_mode == 1 && m_cluster_lighting_pass != nullptr) {
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
		std::vector<std::array<DebugVertex, 3>> triangles;
		triangles.reserve(fill_vertices.size() / 3);
		for (size_t i = 0; i + 2 < fill_vertices.size(); i += 3) {
			triangles.push_back({fill_vertices[i], fill_vertices[i + 1], fill_vertices[i + 2]});
		}
		auto depth = [&](const auto& triangle) {
			const glm::vec3 center = (triangle[0].position + triangle[1].position + triangle[2].position) / 3.0f;
			return (frame->frame_data.view * glm::vec4(center, 1.0f)).z;
		};
		std::stable_sort(triangles.begin(), triangles.end(), [&](const auto& a, const auto& b) { return depth(a) < depth(b); });
		ensureLineCapacity(core, fill_buffer, fill_vertices.size());
		auto* destination = static_cast<DebugVertex*>(fill_buffer.mapped);
		for (const auto& triangle : triangles) {
			std::memcpy(destination, triangle.data(), 3 * sizeof(DebugVertex));
			destination += 3;
		}
		fill_buffer.buffer.getAllocation().flush(0, fill_vertices.size() * sizeof(DebugVertex));
	}
	auto& buffer = m_line_vertex_buffers[frame_index];
	const auto& vertices = frame->debug_line_vertices;

	m_line_vertex_counts[frame_index] = static_cast<uint32_t>(vertices.size());
	if (vertices.empty()) {
		return;
	}

	ensureLineCapacity(core, buffer, vertices.size());
	std::memcpy(buffer.mapped, vertices.data(), vertices.size() * sizeof(DebugVertex));
	buffer.buffer.getAllocation().flush(0, vertices.size() * sizeof(DebugVertex));
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
		cmd.pushConstants(
		    *m_shader_layout.getPipelineLayout(),
		    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		    0,
		    sizeof(pc),
		    &pc
		);
		cmd.bindVertexBuffers(
		    0, std::array<vk::Buffer, 1> {*m_fill_vertex_buffers[frame_index].buffer}, std::array<vk::DeviceSize, 1> {0}
		);
		cmd.draw(m_fill_vertex_counts[frame_index], 1, 0, 0);
	}
	if (line_vertex_count > 0 && m_line_pipeline.isReady()) {
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_line_pipeline.getPipeline());

		const DrawPushConstants pc {};    // identity - line vertices are already in world space
		cmd.pushConstants(
		    *m_shader_layout.getPipelineLayout(),
		    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		    0,
		    sizeof(DrawPushConstants),
		    &pc
		);

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

			constexpr glm::vec4 k_highlight {1.0f, 0.85f, 0.1f, 1.0f};

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

	constexpr glm::vec4 k_red {1.0f, 0.1f, 0.1f, 1.0f};
	constexpr glm::vec4 k_green {0.1f, 1.0f, 0.1f, 1.0f};
	constexpr glm::vec4 k_blue {0.1f, 0.1f, 1.0f, 1.0f};

	std::vector<DebugVertex> vertices;

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
	buffer_ci.size = vertices.size() * sizeof(DebugVertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_gizmo_vertex_buffer, "DebugPass GizmoVertexBuffer");

	void* mapped = m_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(DebugVertex));
	m_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(DebugVertex));
}

void DebugPass::createTranslateGizmoGeometry(const renderer::VulkanCore& core) {
	using namespace toast::gizmo_layout;

	constexpr glm::vec4 k_white {1.0f, 1.0f, 1.0f, 1.0f};

	std::vector<DebugVertex> vertices;

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
	constexpr std::array<glm::vec4, 3> plane_colors {
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
	buffer_ci.size = vertices.size() * sizeof(DebugVertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_translate_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_translate_gizmo_vertex_buffer, "DebugPass TranslateGizmoVertexBuffer");

	void* mapped = m_translate_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(DebugVertex));
	m_translate_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(DebugVertex));
}

void DebugPass::createRotateGizmoGeometry(const renderer::VulkanCore& core) {
	using namespace toast::gizmo_layout;
	constexpr glm::vec4 k_white {1.0f, 1.0f, 1.0f, 1.0f};

	std::vector<DebugVertex> vertices;

	for (int axis = 0; axis < 3; ++axis) {
		const size_t start = vertices.size();
		appendRing(vertices, axis, k_ring_radius, k_ring_thickness, k_ring_segments, k_white);
		m_rotate_gizmo_handles[static_cast<size_t>(k_axis_handles[axis])] = {
		  static_cast<uint32_t>(start), static_cast<uint32_t>(vertices.size() - start), k_axis_colors[axis]
		};
	}

	vk::BufferCreateInfo buffer_ci {};
	buffer_ci.size = vertices.size() * sizeof(DebugVertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_rotate_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_rotate_gizmo_vertex_buffer, "DebugPass RotateGizmoVertexBuffer");

	void* mapped = m_rotate_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(DebugVertex));
	m_rotate_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(DebugVertex));
}

void DebugPass::createScaleGizmoGeometry(const renderer::VulkanCore& core) {
	using namespace toast::gizmo_layout;
	constexpr glm::vec4 k_white {1.0f, 1.0f, 1.0f, 1.0f};

	std::vector<DebugVertex> vertices;

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
	buffer_ci.size = vertices.size() * sizeof(DebugVertex);
	buffer_ci.usage = vk::BufferUsageFlagBits::eVertexBuffer;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	m_scale_gizmo_vertex_buffer = core.getAllocator().createBuffer(buffer_ci, alloc_ci);
	setDebugName(core, *m_scale_gizmo_vertex_buffer, "DebugPass ScaleGizmoVertexBuffer");

	void* mapped = m_scale_gizmo_vertex_buffer.getAllocation().getInfo().pMappedData;
	std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(DebugVertex));
	m_scale_gizmo_vertex_buffer.getAllocation().flush(0, vertices.size() * sizeof(DebugVertex));
}

void DebugPass::ensureLineCapacity(const renderer::VulkanCore& core, DynamicVertexBuffer& buffer, size_t required_vertex_count) {
	const vk::DeviceSize required_bytes = required_vertex_count * sizeof(DebugVertex);
	if (required_bytes <= buffer.capacity_bytes) {
		return;
	}

	const vk::DeviceSize new_capacity = std::max<vk::DeviceSize>(required_bytes * 2, sizeof(DebugVertex) * 1024);

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
