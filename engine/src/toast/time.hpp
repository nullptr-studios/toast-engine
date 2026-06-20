/**
 * @file time.hpp
 * @author Alexey Gurov
 * @date 15 May 2025
 * @brief Controls the time of the application
 *
 * This file was migrated from v1 by Xein on 20 Jun 2026
 */

#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <toast/export.hpp>

namespace toast {
class Engine;
}

struct TOAST_API TimeSnapshot {
	double delta = 0.0;
	double raw_delta = 0.0;
	double render_delta = 0.0;
	double uptime = 0.0;
	uint64_t frame = 0;
	uint64_t render_frame = 0;
	bool paused = false;
};

/**
 *	@brief Class that controls the time of the application
 *
 *	Use @c delta() for time reads
 *
 *	Use @c snapshot() when you need a consistent view of multiple fields for a full system tick
 */
class TOAST_API Time final {
	using clock_t = std::chrono::high_resolution_clock;
	using time_point_t = clock_t::time_point;
	friend struct toast::Engine;

public:
	Time(const Time&) = delete;
	Time& operator=(const Time&) = delete;
	Time(Time&&) = delete;
	Time& operator=(Time&&) = delete;

	~Time();

	static auto get() noexcept -> Time&;                ///< @returns Time instance

	void tick() noexcept;                               ///< Updates the main thread clock
	void renderTick() noexcept;                         ///< Updates the render thread clock

	static auto delta() noexcept -> double;             ///< @returns Time the last @c World::tick() took
	static auto rawDelta() noexcept -> double;          ///< @returns delta() without time scaling
	static auto renderDelta() noexcept -> double;       ///< @returns Time the last render frame took

	static auto frame() noexcept -> uint64_t;           ///< @returns Simulation frame counter
	static auto renderFrame() noexcept -> uint64_t;     ///< @returns Render frame counter

	static auto snapshot() noexcept -> TimeSnapshot;    ///< @returns Consistent snapshot of all time values

	static auto scale() noexcept -> double;             ///< @returns Current time scale multiplier
	static void scale(double value);                    ///< Sets the timescale multiplier

	static void pause() noexcept;                       ///< Pauses simulation time
	static void resume() noexcept;                      ///< Resumes simulation time
	static auto paused() noexcept -> bool;              ///< @returns Whether simulation time is paused

	static auto tps() noexcept -> double;               ///< @returns Simulation ticks per second
	static auto fps() noexcept -> double;               ///< @returns Render frames per second

	static auto uptime() noexcept -> double;            ///< @returns Live seconds since application start
	static auto system() noexcept -> double;            ///< @returns System clock as seconds since epoch
	static void setMaxDelta(double delta);              ///< Sets the maximum delta the simulation will report

private:
	Time();
	static inline Time* instance = nullptr;

	std::atomic<double> m_max_delta {1.0 / 15.0};
	std::atomic<double> m_delta_scale {1.0};

	// main thread state
	time_point_t m_now;
	time_point_t m_previous;
	time_point_t m_start_time;
	uint64_t m_sim_frame = 0;

	// render thread state
	time_point_t m_now_render;
	time_point_t m_previous_render;
	uint64_t m_render_count = 0;

	// published state
	alignas(64) std::atomic<double> m_pub_delta {0.0};
	std::atomic<double> m_pub_raw_delta {0.0};
	std::atomic<uint64_t> m_pub_frame {0};
	std::atomic<bool> m_pub_paused {false};
	std::atomic<bool> m_paused {false};
	std::atomic<uint32_t> m_sim_seq {0};

	alignas(64) std::atomic<double> m_pub_render_delta {0.0};
	std::atomic<uint64_t> m_pub_render_frame {0};
	std::atomic<uint32_t> m_render_seq {0};
};
