/// @file perf_window.cpp
/// @author dario
/// @date 09/10/2026

#include "perf_window.hpp"

#include "../async_compute.hpp"
#include "../vulkan_core.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <imgui.h>
#include <tracy/Tracy.hpp>
#include <vector>

namespace renderer {

namespace {

// Prefixed since unity builds merge this with the other passes

constexpr ImU32 perfColor(int r, int g, int b, int a = 255) {
	return IM_COL32(r, g, b, a);
}

const ImVec4 k_perf_good(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 k_perf_warn(1.0f, 0.72f, 0.25f, 1.0f);
const ImVec4 k_perf_bad(1.0f, 0.38f, 0.38f, 1.0f);

constexpr ImU32 k_perf_bar_good = perfColor(92, 184, 92);
constexpr ImU32 k_perf_bar_warn = perfColor(230, 170, 60);
constexpr ImU32 k_perf_bar_bad = perfColor(220, 80, 80);
constexpr ImU32 k_perf_gpu_line = perfColor(110, 170, 255);
constexpr ImU32 k_perf_budget_line = perfColor(255, 255, 255, 110);
constexpr ImU32 k_perf_graph_bg = perfColor(0, 0, 0, 90);

/// Frame share above which a cost counts as a trend rather than noise
constexpr float k_perf_trend = 0.25f;

auto perfMiB(uint64_t bytes) -> double {
	return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void perfSwatch(ImU32 color) {
	const float size = ImGui::GetTextLineHeight() * 0.7f;
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const float pad = (ImGui::GetTextLineHeight() - size) * 0.5f;
	ImGui::GetWindowDrawList()->AddRectFilled(
	    ImVec2(cursor.x, cursor.y + pad), ImVec2(cursor.x + size, cursor.y + pad + size), color, 2.0f
	);
	ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
	ImGui::SameLine();
}

void perfVerdictText(const char* heading, const ImVec4& color, const char* label, const std::string& detail) {
	ImGui::TextDisabled("%s", heading);
	ImGui::SameLine();
	ImGui::TextColored(color, "%s", label);
	if (!detail.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("%s", detail.c_str());
	}
}

struct PerfSegment {
	std::string_view label;
	float value;
	ImU32 color;
};

void perfStackedBar(const char* id, float width, float height, float total, const std::vector<PerfSegment>& segments) {
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton(id, ImVec2(width, height));
	const bool hovered = ImGui::IsItemHovered();
	const float mouse_x = ImGui::GetIO().MousePos.x;

	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), k_perf_graph_bg, 3.0f);
	if (total <= 0.0f) {
		return;
	}

	float x = origin.x;
	for (const PerfSegment& segment : segments) {
		const float segment_width = std::clamp(segment.value / total, 0.0f, 1.0f) * width;
		if (segment_width < 0.5f) {
			continue;
		}
		const float end = std::min(x + segment_width, origin.x + width);
		draw->AddRectFilled(ImVec2(x, origin.y), ImVec2(end, origin.y + height), segment.color);
		if (hovered && mouse_x >= x && mouse_x < end) {
			draw->AddRect(ImVec2(x, origin.y), ImVec2(end, origin.y + height), perfColor(255, 255, 255, 200));
			ImGui::SetTooltip(
			    "%.*s\n%.2f ms  %.0f%%",
			    static_cast<int>(segment.label.size()),
			    segment.label.data(),
			    segment.value,
			    segment.value / total * 100.0f
			);
		}
		x = end;
	}
}

}

auto PerfWindow::historyOffset() const -> int {
	return m_filled < k_history ? 0 : static_cast<int>(m_cursor);
}

void PerfWindow::record(const GpuTimer* timer) {
	const GpuTimer::Timings* gpu_last = timer != nullptr && timer->last().valid ? &timer->last() : nullptr;
	// The timer keeps its last frame through probe captures and stalls
	const bool fresh_gpu = gpu_last != nullptr && timer->frameSerial() != m_last_gpu_serial;

	m_interval_history[m_cursor] = ImGui::GetIO().DeltaTime * 1000.0f;
	m_gpu_history[m_cursor] = gpu_last != nullptr ? static_cast<float>(gpu_last->graphics_ms) : 0.0f;
	m_cursor = (m_cursor + 1) % k_history;
	m_filled = std::min(m_filled + 1, k_history);

	if (fresh_gpu) {
		m_last_gpu_serial = timer->frameSerial();
		accumulatePasses(*gpu_last);
	}
}

void PerfWindow::accumulatePasses(const GpuTimer::Timings& frame) {
	const auto now = std::chrono::steady_clock::now();
	++m_window_frames;
	m_graphics_sum += frame.graphics_ms;

	// Unknown scopes go right after the previous scope of this frame so existing rows never move
	size_t insert_at = 0;
	for (const auto& scope : frame.scopes) {
		const std::string_view name = scope.name.view();
		auto it =
		    std::ranges::find_if(m_passes, [&](const PassStats& pass) { return pass.depth == scope.depth && pass.name == name; });
		if (it == m_passes.end()) {
			PassStats pass;
			pass.name = std::string(name);
			pass.depth = scope.depth;
			it = m_passes.insert(m_passes.begin() + static_cast<std::ptrdiff_t>(std::min(insert_at, m_passes.size())), std::move(pass));
		}
		insert_at = static_cast<size_t>(it - m_passes.begin()) + 1;

		it->last_seen = now;
		it->gpu_sum += scope.gpu_ms;
		it->cpu_sum += scope.cpu_ms;
		it->gpu_peak_acc = std::max(it->gpu_peak_acc, scope.gpu_ms);
		it->runs_sum += scope.count;
		++it->frames_run;
	}
}

void PerfWindow::publishPasses() {
	if (m_window_frames == 0) {
		return;
	}
	const auto frames = static_cast<double>(m_window_frames);
	m_graphics_avg = static_cast<float>(m_graphics_sum / frames);
	m_graphics_sum = 0.0;

	for (PassStats& pass : m_passes) {
		const double ran = std::max(pass.frames_run, 1u);
		pass.gpu_avg = static_cast<float>(pass.gpu_sum / ran);
		pass.cpu_avg = static_cast<float>(pass.cpu_sum / ran);
		pass.gpu_peak = static_cast<float>(pass.gpu_peak_acc);
		pass.runs = static_cast<float>(static_cast<double>(pass.runs_sum) / ran);
		pass.presence = static_cast<float>(static_cast<double>(pass.frames_run) / frames);

		pass.gpu_sum = 0.0;
		pass.cpu_sum = 0.0;
		pass.gpu_peak_acc = 0.0;
		pass.runs_sum = 0;
		pass.frames_run = 0;
	}
	m_window_frames = 0;

	// Passes gone for a while drop out instead of piling up
	constexpr auto k_forget_after = std::chrono::seconds(10);
	const auto now = std::chrono::steady_clock::now();
	std::erase_if(m_passes, [&](const PassStats& pass) { return now - pass.last_seen > k_forget_after; });
}

auto PerfWindow::heaviestPass(bool by_gpu) const -> const PassStats* {
	// Leaf passes only since a parent time is the sum of its children
	const PassStats* best = nullptr;
	for (size_t i = 0; i < m_passes.size(); ++i) {
		const PassStats& pass = m_passes[i];
		if (i + 1 < m_passes.size() && m_passes[i + 1].depth > pass.depth) {
			continue;
		}
		const float value = by_gpu ? pass.perFrame() : pass.cpu_avg * pass.presence;
		if (best == nullptr || value > (by_gpu ? best->perFrame() : best->cpu_avg * best->presence)) {
			best = &pass;
		}
	}
	return best;
}

void PerfWindow::sample(const VulkanRenderer::PerfCounters& counters, uint32_t skipped) {
	constexpr auto k_sample_interval = std::chrono::milliseconds(500);
	const auto now = std::chrono::steady_clock::now();
	const bool first_sample = m_last_sample.time_since_epoch().count() == 0;
	const bool sample_due = !first_sample && now - m_last_sample >= k_sample_interval;

	if (sample_due) {
		const double seconds = std::chrono::duration<double>(now - m_last_sample).count();
		const VulkanRenderer::PerfCounters& last = m_last_counters;
		const auto draws = static_cast<double>(counters.draws - last.draws);
		const auto built = static_cast<double>(counters.frames_built - last.frames_built);
		const auto ticks = static_cast<double>(counters.ticks - last.ticks);

		const auto per = [](uint64_t current, uint64_t previous, double count) {
			return count > 0.0 ? static_cast<float>(static_cast<double>(current - previous) / count / 1.0e6) : 0.0f;
		};

		Sample& s = m_sample;
		s.render_fps = static_cast<float>(draws / seconds);
		s.game_fps = static_cast<float>(built / seconds);
		s.tick_rate = static_cast<float>(ticks / seconds);
		s.skipped_per_second = static_cast<float>(static_cast<double>(skipped - m_last_skipped) / seconds);
		s.repeated_ratio =
		    draws > 0.0 ? static_cast<float>(static_cast<double>(counters.repeated_draws - last.repeated_draws) / draws) : 0.0f;
		s.wait_ratio =
		    std::min(1.0f, static_cast<float>(static_cast<double>(counters.slot_wait_ns - last.slot_wait_ns) / 1.0e9 / seconds));

		s.draw_ms = per(counters.draw_work_ns, last.draw_work_ns, draws);
		s.upload_ms = per(counters.upload_ns, last.upload_ns, draws);
		s.gpu_wait_ms = per(counters.gpu_wait_ns, last.gpu_wait_ns, draws);
		s.acquire_ms = per(counters.acquire_ns, last.acquire_ns, draws);
		s.record_ms = per(counters.record_ns, last.record_ns, draws);
		s.submit_ms = per(counters.submit_ns, last.submit_ns, draws);
		s.present_ms = per(counters.present_ns, last.present_ns, draws);
		s.frame_wait_ms = per(counters.frame_wait_ns, last.frame_wait_ns, draws);
		s.pacing_ms = per(counters.pacing_ns, last.pacing_ns, draws);
		s.build_ms = per(counters.build_ns, last.build_ns, built);
		s.slot_wait_ms = per(counters.slot_wait_ns, last.slot_wait_ns, ticks);

		History sorted = m_interval_history;
		std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(m_filled));
		s.p99_interval_ms = m_filled > 0 ? sorted[std::min(m_filled - 1, m_filled * 99 / 100)] : 0.0f;
		s.max_interval_ms = m_filled > 0 ? sorted[m_filled - 1] : 0.0f;

		publishPasses();
		m_has_sample = true;
	}

	if (first_sample || sample_due) {
		m_last_sample = now;
		m_last_counters = counters;
		m_last_skipped = skipped;
	}
}

auto PerfWindow::phases() const -> std::array<Phase, 9> {
	const Sample& s = m_sample;
	const float accounted =
	    s.gpu_wait_ms + s.record_ms + s.submit_ms + s.upload_ms + s.acquire_ms + s.present_ms + s.frame_wait_ms + s.pacing_ms;
	return {
	  Phase {"GPU wait", s.gpu_wait_ms, perfColor(220, 90, 90)},
	  Phase {"Record", s.record_ms, perfColor(90, 150, 230)},
	  Phase {"Submit", s.submit_ms, perfColor(160, 110, 220)},
	  Phase {"Uploads", s.upload_ms, perfColor(70, 190, 180)},
	  Phase {"Acquire", s.acquire_ms, perfColor(235, 150, 60)},
	  Phase {"Present", s.present_ms, perfColor(230, 210, 80)},
	  Phase {"Game wait", s.frame_wait_ms, perfColor(130, 130, 130)},
	  Phase {"Cap sleep", s.pacing_ms, perfColor(70, 130, 80)},
	  Phase {"Other", std::max(0.0f, m_interval_ms - accounted), perfColor(80, 80, 80)},
	};
}

auto PerfWindow::renderVerdict(bool has_gpu) const -> Verdict {
	const Sample& s = m_sample;
	const auto share = [this](float ms) { return m_interval_ms > 0.0f ? ms / m_interval_ms : 0.0f; };
	const auto top = [this](bool by_gpu) -> std::string {
		const PassStats* pass = heaviestPass(by_gpu);
		if (pass == nullptr) {
			return {};
		}
		return std::format("top: {} {:.2f} ms", pass->name, by_gpu ? pass->perFrame() : pass->cpu_avg * pass->presence);
	};

	const float cpu_work_ms = s.upload_ms + s.record_ms + s.submit_ms;
	const float display_ms = s.acquire_ms + s.present_ms;
	const float idle_ms = s.frame_wait_ms + s.pacing_ms;

	if (share(s.gpu_wait_ms) >= k_perf_trend || (has_gpu && share(m_gpu_ms) >= 0.85f && share(idle_ms) < k_perf_trend)) {
		return {.label = "GPU bound", .bad = true, .detail = top(true)};
	}
	if (share(display_ms) >= k_perf_trend) {
		return {.label = "Display paced", .warn = true, .detail = std::format("acquire+present {:.0f}%", share(display_ms) * 100.0f)};
	}
	if (share(cpu_work_ms) >= 0.6f) {
		return {.label = "CPU bound", .bad = true, .detail = top(false)};
	}
	if (share(idle_ms) >= k_perf_trend) {
		return {.label = "Headroom", .detail = std::format("idle {:.0f}%", share(idle_ms) * 100.0f)};
	}
	return {.label = "Balanced", .detail = {}};
}

auto PerfWindow::gameVerdict() const -> Verdict {
	const Sample& s = m_sample;
	const auto cap = static_cast<float>(VulkanRenderer::instance->effectiveFrameRateLimit());
	const bool at_cap = cap > 0.0f && s.render_fps >= cap * 0.95f;

	if (s.skipped_per_second > 0.0f || (!at_cap && s.wait_ratio >= k_perf_trend)) {
		return {
		  .label = "Renderer exhausted",
		  .bad = true,
		  .detail = std::format("wait {:.0f}%  skipped {:.1f}/s", s.wait_ratio * 100.0f, s.skipped_per_second)
		};
	}
	if (s.repeated_ratio >= k_perf_trend) {
		return {.label = "CPU starved", .warn = true, .detail = std::format("repeated {:.0f}%", s.repeated_ratio * 100.0f)};
	}
	if (at_cap) {
		return {.label = "Capped", .detail = {}};
	}
	return {.label = "Balanced", .detail = {}};
}

void PerfWindow::drawFrameGraph(float width, float height) {
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##perf_frame_graph", ImVec2(width, height));
	const bool hovered = ImGui::IsItemHovered();

	// Budget and the recent worst case stay on screen
	const float target = std::max({m_sample.p99_interval_ms * 1.2f, m_budget_ms * 1.5f, m_gpu_ms * 1.5f, 1.0f});
	m_graph_scale_ms = m_graph_scale_ms <= 0.0f ? target : m_graph_scale_ms + ((target - m_graph_scale_ms) * 0.05f);
	const float scale = m_graph_scale_ms;

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 end(origin.x + width, origin.y + height);
	draw->AddRectFilled(origin, end, k_perf_graph_bg, 3.0f);

	const float bar_width = width / static_cast<float>(k_history);
	const auto y_of = [&](float ms) { return end.y - (std::clamp(ms / scale, 0.0f, 1.0f) * height); };
	const int offset = historyOffset();
	const auto count = static_cast<int>(m_filled);
	// Newest frame at the right edge
	const float first_x = end.x - (bar_width * static_cast<float>(count));

	std::array<ImVec2, k_history> gpu_points {};
	int hovered_index = -1;
	for (int i = 0; i < count; ++i) {
		const size_t index = static_cast<size_t>((offset + i) % static_cast<int>(k_history));
		const float ms = m_interval_history[index];
		const float x0 = first_x + (bar_width * static_cast<float>(i));
		const ImU32 color =
		    ms <= m_budget_ms * 1.05f ? k_perf_bar_good : (ms <= m_budget_ms * 1.5f ? k_perf_bar_warn : k_perf_bar_bad);
		draw->AddRectFilled(ImVec2(x0, y_of(ms)), ImVec2(x0 + std::max(bar_width - 0.5f, 1.0f), end.y), color);
		gpu_points[static_cast<size_t>(i)] = ImVec2(x0 + (bar_width * 0.5f), y_of(m_gpu_history[index]));

		if (hovered && ImGui::GetIO().MousePos.x >= x0 && ImGui::GetIO().MousePos.x < x0 + bar_width) {
			hovered_index = static_cast<int>(index);
			draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x0 + bar_width, end.y), perfColor(255, 255, 255, 40));
		}
	}
	if (count > 1 && m_gpu_ms > 0.0f) {
		draw->AddPolyline(gpu_points.data(), count, k_perf_gpu_line, ImDrawFlags_None, 1.5f);
	}

	const float budget_y = y_of(m_budget_ms);
	for (float x = origin.x; x < end.x; x += 8.0f) {
		draw->AddLine(ImVec2(x, budget_y), ImVec2(std::min(x + 4.0f, end.x), budget_y), k_perf_budget_line, 1.0f);
	}
	const std::string budget_label = std::format("{:.1f} ms", m_budget_ms);
	draw->AddText(ImVec2(origin.x + 4.0f, budget_y - ImGui::GetTextLineHeight()), k_perf_budget_line, budget_label.c_str());
	const std::string scale_label = std::format("{:.0f} ms", scale);
	draw->AddText(ImVec2(origin.x + 4.0f, origin.y + 1.0f), perfColor(255, 255, 255, 90), scale_label.c_str());

	if (hovered_index >= 0) {
		const auto index = static_cast<size_t>(hovered_index);
		ImGui::SetTooltip("Frame %.2f ms\nGPU   %.2f ms", m_interval_history[index], m_gpu_history[index]);
	}
}

void PerfWindow::drawPhaseBar(float width, float height) const {
	std::vector<PerfSegment> segments;
	for (const Phase& phase : phases()) {
		segments.push_back({phase.label, phase.ms, phase.color});
	}
	perfStackedBar("##perf_phase_bar", width, height, m_interval_ms, segments);
}

void PerfWindow::drawSummary(bool has_gpu) {
	const Sample& s = m_sample;
	auto* renderer = VulkanRenderer::instance;
	const auto cap = static_cast<float>(renderer->effectiveFrameRateLimit());

	// NOLINTNEXTLINE(readability-avoid-nested-conditional-operator)
	const ImVec4 fps_color = m_interval_ms <= m_budget_ms * 1.05f  ? k_perf_good
	                         : m_interval_ms <= m_budget_ms * 1.5f ? k_perf_warn
	                                                               : k_perf_bad;
	ImGui::TextColored(fps_color, "%5.1f fps", s.render_fps);
	ImGui::SameLine();
	ImGui::Text("%6.2f ms", m_interval_ms);
	ImGui::SameLine();
	ImGui::TextDisabled("p99 %.1f  max %.1f", s.p99_interval_ms, s.max_interval_ms);

	ImGui::TextDisabled("GPU ");
	ImGui::SameLine();
	if (has_gpu) {
		ImGui::TextColored(ImVec4(0.43f, 0.67f, 1.0f, 1.0f), "%5.2f ms", m_gpu_ms);
	} else {
		ImGui::TextDisabled("n/a     ");
	}
	ImGui::SameLine();
	ImGui::TextDisabled(" CPU %5.2f ms   budget %.1f ms", s.upload_ms + s.record_ms + s.submit_ms, m_budget_ms);
	ImGui::SameLine();
	if (cap > 0.0f) {
		ImGui::TextDisabled("(%.0f fps cap%s)", cap, renderer->applicationFocused() ? "" : ", unfocused");
	} else {
		ImGui::TextDisabled("(uncapped)");
	}

	const float width = ImGui::GetFontSize() * 30.0f;
	drawFrameGraph(width, ImGui::GetFontSize() * 4.0f);
	drawPhaseBar(width, ImGui::GetFontSize() * 0.6f);

	const Verdict render = renderVerdict(has_gpu);
	perfVerdictText(
	    "Render", render.bad ? k_perf_bad : (render.warn ? k_perf_warn : k_perf_good), render.label, render.detail
	);    // NOLINT(readability-avoid-nested-conditional-operator)
	const Verdict game = gameVerdict();
	perfVerdictText(
	    "Game  ", game.bad ? k_perf_bad : (game.warn ? k_perf_warn : k_perf_good), game.label, game.detail
	);    // NOLINT(readability-avoid-nested-conditional-operator)
}

void PerfWindow::drawFrameTab() {
	const Sample& s = m_sample;
	ImGui::SeparatorText("Render thread, per frame");
	if (ImGui::BeginTable("##perf_phases", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
		for (const Phase& phase : phases()) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			perfSwatch(phase.color);
			ImGui::TextUnformatted(phase.label);
			ImGui::TableNextColumn();
			ImGui::Text("%6.2f ms", phase.ms);
			ImGui::TableNextColumn();
			const float fraction = m_interval_ms > 0.0f ? std::clamp(phase.ms / m_interval_ms, 0.0f, 1.0f) : 0.0f;
			const std::string label = std::format("{:.0f}%", fraction * 100.0f);
			ImGui::ProgressBar(fraction, ImVec2(ImGui::GetFontSize() * 12.0f, 0.0f), label.c_str());
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Game thread");
	if (ImGui::BeginTable("##perf_game", 2, ImGuiTableFlags_SizingFixedFit)) {
		const auto row = [](const char* label, const std::string& value) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(value.c_str());
		};
		row("Ticks", std::format("{:.0f} /s", s.tick_rate));
		row("Frames built", std::format("{:.1f} /s  {:.2f} ms each", s.game_fps, s.build_ms));
		row("Slot wait", std::format("{:.2f} ms  {:.0f}% of the time", s.slot_wait_ms, s.wait_ratio * 100.0f));
		row("Repeated draws", std::format("{:.0f}%", s.repeated_ratio * 100.0f));
		row("Skipped builds", std::format("{:.1f} /s", s.skipped_per_second));
		ImGui::EndTable();
	}
	ImGui::TextDisabled(
	    "Repeated: the renderer drew a frame the game had not rebuilt.\nSkipped: the game built a frame the renderer never drew."
	);
}

void PerfWindow::drawGpuTab() {
	const GpuTimer* timer = VulkanRenderer::instance->gpuTimer();
	if (timer == nullptr || !timer->supported()) {
		ImGui::TextDisabled("This device has no timestamp queries");
		return;
	}
	if (m_passes.empty()) {
		ImGui::TextDisabled("No GPU timings yet");
		return;
	}

	const bool has_baseline = m_graphics_baseline >= 0.0f;
	const float width = ImGui::GetFontSize() * 36.0f;

	ImGui::Text("Graphics queue %.2f ms/frame", m_graphics_avg);
	if (has_baseline) {
		const float delta = m_graphics_avg - m_graphics_baseline;
		ImGui::SameLine();
		ImGui::TextColored(delta <= 0.0f ? k_perf_good : k_perf_bad, "%+.2f ms", delta);
		ImGui::SameLine();
		ImGui::TextDisabled("vs baseline %.2f", m_graphics_baseline);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Set baseline")) {
		m_graphics_baseline = m_graphics_avg;
		for (PassStats& pass : m_passes) {
			pass.baseline = pass.perFrame();
		}
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Remember the current costs; the table then shows each pass's change against them");
	}
	if (has_baseline) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear")) {
			m_graphics_baseline = -1.0f;
			for (PassStats& pass : m_passes) {
				pass.baseline = -1.0f;
			}
		}
	}

	// Queue time left after the top level passes is GPU work outside any scope
	constexpr std::array k_pass_colors {
	  perfColor(90, 150, 230),
	  perfColor(92, 184, 92),
	  perfColor(230, 170, 60),
	  perfColor(160, 110, 220),
	  perfColor(70, 190, 180),
	  perfColor(220, 90, 90),
	  perfColor(230, 210, 80),
	  perfColor(200, 120, 170),
	};
	std::vector<PerfSegment> segments;
	float scoped_ms = 0.0f;
	for (const PassStats& pass : m_passes) {
		if (pass.depth == 0) {
			segments.push_back({pass.name, pass.perFrame(), k_pass_colors[segments.size() % k_pass_colors.size()]});
			scoped_ms += pass.perFrame();
		}
	}
	const float unscoped_ms = std::max(0.0f, m_graphics_avg - scoped_ms);
	segments.push_back({"Unscoped", unscoped_ms, perfColor(70, 70, 70)});
	perfStackedBar("##perf_gpu_bar", width, ImGui::GetFontSize() * 0.9f, m_graphics_avg, segments);

	ImGui::Checkbox("Hide idle passes", &m_hide_idle_passes);
	ImGui::SameLine();
	ImGui::TextDisabled("Averaged every 0.5 s. Hover a row for details");

	enum Column : uint8_t {
		pass_column,
		gpu_column,
		peak_column,
		cpu_column,
		runs_column,
		delta_column
	};

	constexpr ImGuiTableFlags k_flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_RowBg |
	                                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
	                                    ImGuiTableFlags_ScrollY;
	// Fixed sizes so the auto sized window does not jitter as rows and digits change
	const ImVec2 outer(width, ImGui::GetTextLineHeightWithSpacing() * 18.0f);
	const int column_count = has_baseline ? 6 : 5;
	if (!ImGui::BeginTable("##perf_gpu_passes", column_count, k_flags, outer)) {
		return;
	}
	const float digits = ImGui::CalcTextSize("00.00").x;
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch, 0.0f, pass_column);
	ImGui::TableSetupColumn(
	    "GPU ms", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, digits, gpu_column
	);
	ImGui::TableSetupColumn(
	    "Peak", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, digits, peak_column
	);
	ImGui::TableSetupColumn(
	    "CPU ms", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, digits, cpu_column
	);
	ImGui::TableSetupColumn(
	    "Runs",
	    ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed,
	    ImGui::CalcTextSize("1 @ 100%").x,
	    runs_column
	);
	if (has_baseline) {
		ImGui::TableSetupColumn(
		    "Change", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, digits * 1.2f, delta_column
		);
	}
	ImGui::TableHeadersRow();

	const auto idle = [](const PassStats& pass) { return pass.perFrame() < 0.005f && pass.cpu_avg * pass.presence < 0.005f; };
	std::vector<size_t> order;
	order.reserve(m_passes.size());
	for (size_t i = 0; i < m_passes.size(); ++i) {
		if (!m_hide_idle_passes || !idle(m_passes[i])) {
			order.push_back(i);
		}
	}

	// Flat when sorted since nesting only reads right in frame order
	bool sorted = false;
	if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
		const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
		const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
		const auto key = [&](size_t i) -> float {
			const PassStats& pass = m_passes[i];
			switch (spec.ColumnUserID) {
				case peak_column: return pass.gpu_peak;
				case cpu_column: return pass.cpu_avg;
				case runs_column: return pass.runs * pass.presence;
				case delta_column: return pass.baseline >= 0.0f ? pass.perFrame() - pass.baseline : pass.perFrame();
				default: return pass.perFrame();
			}
		};
		std::ranges::stable_sort(order, [&](size_t a, size_t b) { return ascending ? key(a) < key(b) : key(a) > key(b); });
		sorted = true;
	}

	for (const size_t i : order) {
		const PassStats& pass = m_passes[i];
		const bool is_idle = idle(pass);
		const bool absent = pass.presence <= 0.0f;
		ImGui::TableNextRow();
		ImGui::PushID(static_cast<int>(i));

		ImGui::TableNextColumn();
		const int indent = sorted ? 0 : static_cast<int>(pass.depth * 2);
		if (is_idle || absent) {
			ImGui::TextDisabled("%*s%s", indent, "", pass.name.c_str());
		} else {
			ImGui::Text("%*s%s", indent, "", pass.name.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(
			    "%s\n"
			    "GPU   %.3f ms when it runs, %.3f ms per frame\n"
			    "Peak  %.3f ms (worst frame of the last 0.5 s)\n"
			    "CPU   %.3f ms recording\n"
			    "Runs  %.1f per frame, in %.0f%% of frames",
			    pass.name.c_str(),
			    pass.gpu_avg,
			    pass.perFrame(),
			    pass.gpu_peak,
			    pass.cpu_avg,
			    pass.runs,
			    pass.presence * 100.0f
			);
		}

		ImGui::TableNextColumn();
		ImGui::Text("%5.2f", pass.perFrame());

		// Peak well above average means a hitch rather than a steady cost
		ImGui::TableNextColumn();
		const bool spiky = pass.gpu_peak > 0.25f && pass.gpu_peak > pass.gpu_avg * 2.0f;
		if (spiky) {
			ImGui::TextColored(k_perf_warn, "%5.2f", pass.gpu_peak);
		} else {
			ImGui::TextDisabled("%5.2f", pass.gpu_peak);
		}

		ImGui::TableNextColumn();
		ImGui::Text("%5.2f", pass.cpu_avg * pass.presence);

		ImGui::TableNextColumn();
		if (absent) {
			ImGui::TextDisabled("not run");
		} else if (pass.presence < 0.995f) {
			ImGui::Text("%.0f @ %.0f%%", pass.runs, pass.presence * 100.0f);
		} else {
			ImGui::Text("%.0f", pass.runs);
		}

		if (has_baseline) {
			ImGui::TableNextColumn();
			if (pass.baseline < 0.0f) {
				ImGui::TextColored(k_perf_warn, "new");
			} else {
				const float delta = pass.perFrame() - pass.baseline;
				if (std::abs(delta) < 0.02f) {
					ImGui::TextDisabled("  =");
				} else {
					ImGui::TextColored(delta < 0.0f ? k_perf_good : k_perf_bad, "%+.2f", delta);
				}
			}
		}
		ImGui::PopID();
	}

	// So the column sums to the queue total
	if (!sorted && unscoped_ms >= 0.01f) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("Unscoped");
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%5.2f", unscoped_ms);
	}
	ImGui::EndTable();
}

void PerfWindow::drawMemoryTab() {
	auto* renderer = VulkanRenderer::instance;
	const VulkanCore& core = renderer->getCore();

	ImGui::SeparatorText("GPU memory heaps");
	if (!core.isMemoryBudgetSupported()) {
		ImGui::TextColored(k_perf_warn, "Estimated: VK_EXT_memory_budget is unavailable");
	}
	const auto budgets = core.getAllocator().getHeapBudgets();
	const auto properties = core.getPhysicalDevice().getMemoryProperties();
	for (uint32_t heap = 0; heap < properties.memoryHeapCount && heap < budgets.size(); ++heap) {
		const auto& budget = budgets[heap];
		if (budget.budget == 0) {
			continue;
		}
		const bool device_local =
		    (properties.memoryHeaps[heap].flags & vk::MemoryHeapFlagBits::eDeviceLocal) != vk::MemoryHeapFlags {};
		const float fraction = std::clamp(static_cast<float>(budget.usage) / static_cast<float>(budget.budget), 0.0f, 1.0f);

		ImGui::Text("Heap %u  %s", heap, device_local ? "device local" : "host");
		ImGui::SameLine();
		ImGui::TextDisabled("%.0f MiB total", perfMiB(properties.memoryHeaps[heap].size));

		const std::string label = std::format("{:.0f} / {:.0f} MiB", perfMiB(budget.usage), perfMiB(budget.budget));
		ImGui::PushStyleColor(
		    ImGuiCol_PlotHistogram, fraction < 0.75f ? k_perf_good : (fraction < 0.9f ? k_perf_warn : k_perf_bad)
		);    // NOLINT(readability-avoid-nested-conditional-operator)
		ImGui::ProgressBar(fraction, ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f), label.c_str());
		ImGui::PopStyleColor();

		// Usage beyond the VMA blocks is other processes and drivers
		const auto& stats = budget.statistics;
		const double packing =
		    stats.blockBytes > 0 ? static_cast<double>(stats.allocationBytes) / static_cast<double>(stats.blockBytes) * 100.0 : 0.0;
		ImGui::TextDisabled(
		    "  engine %.1f MiB in %u allocations, %u blocks of %.1f MiB (%.0f%% packed)",
		    perfMiB(stats.allocationBytes),
		    stats.allocationCount,
		    stats.blockCount,
		    perfMiB(stats.blockBytes),
		    packing
		);
	}

	ImGui::SeparatorText("Uploads");
	const VulkanRenderer::UploadStats uploads = renderer->uploadStats();
	ImGui::Text("Transfer slots   %u / %u in flight", uploads.slots_in_flight, uploads.slot_count);
	ImGui::Text("Batches pending  %zu", uploads.batches_in_flight);
	ImGui::Text("Jobs waiting     %zu", uploads.waiting_jobs);
	const float staging =
	    uploads.host_budget > 0
	        ? std::clamp(static_cast<float>(uploads.host_bytes) / static_cast<float>(uploads.host_budget), 0.0f, 1.0f)
	        : 0.0f;
	const std::string staging_label =
	    std::format("staging {:.1f} / {:.0f} MiB", perfMiB(uploads.host_bytes), perfMiB(uploads.host_budget));
	ImGui::ProgressBar(staging, ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f), staging_label.c_str());

	ImGui::SeparatorText("Async compute");
	const AsyncCompute& compute = core.asyncCompute();
	const AsyncCompute::Ticket submitted = compute.lastSubmittedTicket();
	const AsyncCompute::Ticket completed = compute.completedTicket();
	ImGui::Text("Queue family     %u", compute.queueFamilyIndex());
	ImGui::Text("Jobs in flight   %llu", static_cast<unsigned long long>(submitted > completed ? submitted - completed : 0));
	ImGui::TextDisabled(
	    "Tickets: %llu submitted, %llu completed",
	    static_cast<unsigned long long>(submitted),
	    static_cast<unsigned long long>(completed)
	);
}

void PerfWindow::drawSceneTab() {
	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		ImGui::TextDisabled("No frame");
		return;
	}
	const auto visible_meshes = std::ranges::count_if(frame->mesh_instances, [](const auto& proxy) { return proxy.visible; });
	const auto visible_voxels = std::ranges::count_if(frame->voxel_instances, [](const auto& proxy) { return proxy.visible; });

	if (ImGui::BeginTable("##perf_scene", 2, ImGuiTableFlags_SizingFixedFit)) {
		const auto row = [](const char* label, const std::string& value) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(value.c_str());
		};
		row("Mesh instances", std::format("{} visible of {}", visible_meshes, frame->mesh_instances.size()));
		row("Material ranges", std::format("{}", frame->material_ranges.size()));
		row("Voxel volumes", std::format("{} visible of {}", visible_voxels, frame->voxel_instances.size()));
		row("Lights", std::format("{} of {} submitted", frame->lights.size(), frame->light_stats.submitted));
		ImGui::EndTable();
	}
}

void PerfWindow::draw() {
	ZoneScoped;
	auto* renderer = VulkanRenderer::instance;
	const GpuTimer* timer = renderer->gpuTimer();
	m_has_gpu = timer != nullptr && timer->last().valid;

	record(timer);
	sample(renderer->perfCounters(), renderer->getSkippedBuildCount());

	const auto cap = static_cast<float>(renderer->effectiveFrameRateLimit());
	m_budget_ms = cap > 0.0f ? 1000.0f / cap : 1000.0f / 60.0f;
	m_interval_ms = m_sample.render_fps > 0.0f ? 1000.0f / m_sample.render_fps : 0.0f;
	// Published value so the text does not flicker
	m_gpu_ms = m_graphics_avg;

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, 12.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.88f);
	constexpr ImGuiWindowFlags k_flags =
	    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

	const bool open = ImGui::Begin("Performance", nullptr, k_flags);
	if (!open || !m_has_sample) {
		if (open) {
			ImGui::TextDisabled("Sampling...");
		}
		ImGui::End();
		return;
	}

	drawSummary(m_has_gpu);

	if (ImGui::SmallButton(m_expanded ? "Hide details" : "Details")) {
		m_expanded = !m_expanded;
	}

	if (m_expanded && ImGui::BeginTabBar("##perf_tabs")) {
		if (ImGui::BeginTabItem("Frame")) {
			drawFrameTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("GPU")) {
			drawGpuTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Memory")) {
			drawMemoryTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Scene")) {
			drawSceneTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

}
