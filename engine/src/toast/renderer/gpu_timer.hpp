/// @file gpu_timer.hpp

#pragma once
#include "vulkan_common.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>

namespace renderer {
class VulkanCore;

/// @note Render thread only
class GpuTimer {
public:
	static constexpr uint32_t k_max_scopes = 64;
	static constexpr uint32_t k_no_scope = ~uint32_t {0};

	/// Copied since a pass can be destroyed before its frame is read back
	struct ScopeName {
		std::array<char, 39> chars {};
		uint8_t length = 0;

		ScopeName() = default;
		explicit ScopeName(std::string_view text) noexcept;

		[[nodiscard]]
		auto view() const noexcept -> std::string_view {
			return {chars.data(), length};
		}
	};

	struct ScopeTiming {
		ScopeName name;
		uint32_t depth = 0;
		uint32_t count = 0;
		double cpu_ms = 0.0;
		double gpu_ms = 0.0;
	};

	struct Timings {
		bool valid = false;
		double graphics_ms = 0.0;
		double compute_ms = 0.0;
		std::vector<ScopeTiming> scopes;
	};

	GpuTimer(const VulkanCore& core, uint32_t frame_slots);

	[[nodiscard]]
	auto supported() const noexcept -> bool {
		return m_supported;
	}

	[[nodiscard]]
	auto computeSupported() const noexcept -> bool {
		return m_compute_supported;
	}

	void beginGraphics(uint32_t slot, vk::CommandBuffer cmd);
	void endGraphics(uint32_t slot, vk::CommandBuffer cmd);
	void beginCompute(uint32_t slot, vk::CommandBuffer cmd);
	void endCompute(uint32_t slot, vk::CommandBuffer cmd);

	auto beginScope(uint32_t slot, vk::CommandBuffer cmd, std::string_view name) -> uint32_t;
	void endScope(uint32_t slot, vk::CommandBuffer cmd, uint32_t scope);

	/// @note Only once the graphics and compute fences of the slot have signalled
	void collect(uint32_t slot);

	[[nodiscard]]
	auto last() const noexcept -> const Timings& {
		return m_frame;
	}

	[[nodiscard]]
	auto smoothed() const noexcept -> const Timings& {
		return m_smoothed;
	}

private:
	struct PendingScope {
		ScopeName name;
		uint32_t depth = 0;
		std::chrono::steady_clock::time_point cpu_start {};
		double cpu_ms = 0.0;
	};

	struct Slot {
		std::vector<PendingScope> scopes;
		uint32_t depth = 0;
		bool graphics_written = false;
		bool compute_written = false;
	};

	/// graphics begin and end then a pair per scope then compute begin and end
	static constexpr uint32_t k_queries_per_slot = 2 + (2 * k_max_scopes) + 2;

	[[nodiscard]]
	static auto base(uint32_t slot) noexcept -> uint32_t {
		return slot * k_queries_per_slot;
	}

	[[nodiscard]]
	auto ticksToMs(uint64_t begin, uint64_t end, uint64_t mask) const noexcept -> double;

	vk::raii::QueryPool m_pool = nullptr;
	std::vector<Slot> m_slots;
	double m_ns_per_tick = 1.0;
	uint64_t m_graphics_mask = ~uint64_t {0};
	uint64_t m_compute_mask = ~uint64_t {0};
	bool m_supported = false;
	bool m_compute_supported = false;
	bool m_reported_unavailable = false;

	Timings m_frame;
	Timings m_smoothed;
	Timings m_scratch;
};

class GpuScope {
public:
	GpuScope(GpuTimer* timer, uint32_t slot, vk::CommandBuffer cmd, std::string_view name)
	    : m_timer(timer),
	      m_slot(slot),
	      m_cmd(cmd),
	      m_scope(timer != nullptr ? timer->beginScope(slot, cmd, name) : GpuTimer::k_no_scope) { }

	~GpuScope() {
		if (m_timer != nullptr) {
			m_timer->endScope(m_slot, m_cmd, m_scope);
		}
	}

	GpuScope(const GpuScope&) = delete;
	auto operator=(const GpuScope&) -> GpuScope& = delete;
	GpuScope(GpuScope&&) = delete;
	auto operator=(GpuScope&&) -> GpuScope& = delete;

private:
	GpuTimer* m_timer;
	uint32_t m_slot;
	vk::CommandBuffer m_cmd;
	uint32_t m_scope;
};

}
