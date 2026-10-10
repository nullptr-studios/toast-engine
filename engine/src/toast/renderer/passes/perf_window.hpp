/// @file perf_window.hpp
/// @author dario
/// @date 09/10/2026

#pragma once

#include "../vulkan_renderer.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace renderer {

class PerfWindow {
public:
	/// Render thread only
	void draw();

private:
	static constexpr size_t k_history = 240;
	using History = std::array<float, k_history>;

	/// Rates and per frame averages over the last sample interval
	struct Sample {
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

		float p99_interval_ms = 0.0f;
		float max_interval_ms = 0.0f;
	};

	struct Phase {
		const char* label;
		float ms;
		uint32_t color;
	};

	/// Kept across frames so rows never jump and published once per sample interval
	struct PassStats {
		std::string name;
		uint32_t depth = 0;
		std::chrono::steady_clock::time_point last_seen;

		// Accumulated since the last publish
		double gpu_sum = 0.0;
		double cpu_sum = 0.0;
		double gpu_peak_acc = 0.0;
		uint32_t runs_sum = 0;
		uint32_t frames_run = 0;

		// Published values the table shows
		float gpu_avg = 0.0f;      ///< per frame it ran
		float gpu_peak = 0.0f;     ///< worst single frame
		float cpu_avg = 0.0f;
		float runs = 0.0f;         ///< per frame it ran
		float presence = 0.0f;     ///< share of frames it ran in
		float baseline = -1.0f;    ///< per frame cost at baseline and negative when unset

		/// Frames it did not run count as zero
		[[nodiscard]]
		auto perFrame() const -> float {
			return gpu_avg * presence;
		}
	};

	struct Verdict {
		const char* label = "";
		bool bad = false;
		bool warn = false;
		std::string detail;
	};

	void record(const GpuTimer* timer);
	void accumulatePasses(const GpuTimer::Timings& frame);
	void publishPasses();
	[[nodiscard]]
	auto heaviestPass(bool by_gpu) const -> const PassStats*;
	void sample(const VulkanRenderer::PerfCounters& counters, uint32_t skipped);

	[[nodiscard]]
	auto phases() const -> std::array<Phase, 9>;
	[[nodiscard]]
	auto renderVerdict(bool has_gpu) const -> Verdict;
	[[nodiscard]]
	auto gameVerdict() const -> Verdict;

	void drawSummary(bool has_gpu);
	void drawFrameGraph(float width, float height);
	void drawPhaseBar(float width, float height) const;
	void drawFrameTab();
	void drawGpuTab();
	void drawMemoryTab();
	void drawSceneTab();

	[[nodiscard]]
	auto historyOffset() const -> int;

	std::chrono::steady_clock::time_point m_last_sample;
	VulkanRenderer::PerfCounters m_last_counters {};
	uint32_t m_last_skipped = 0;
	bool m_has_sample = false;
	Sample m_sample;

	History m_interval_history {};
	History m_gpu_history {};
	/// Frame order with new passes inserted after their predecessor
	std::vector<PassStats> m_passes;
	uint64_t m_last_gpu_serial = 0;
	uint32_t m_window_frames = 0;
	double m_graphics_sum = 0.0;
	float m_graphics_avg = 0.0f;
	float m_graphics_baseline = -1.0f;
	size_t m_cursor = 0;
	size_t m_filled = 0;

	/// Eased so spikes do not rescale the graph
	float m_graph_scale_ms = 0.0f;
	/// From the frame rate cap or 60 fps when uncapped
	float m_budget_ms = 1000.0f / 60.0f;
	float m_interval_ms = 0.0f;
	float m_gpu_ms = 0.0f;

	bool m_expanded = false;
	bool m_hide_idle_passes = true;
	bool m_has_gpu = false;
};

}
