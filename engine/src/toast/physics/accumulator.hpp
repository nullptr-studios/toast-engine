/**
 * @file Accumulator.hpp
 * @author Xein
 * @date 09 Sep 2026
 * @brief Handles accumulating the physics steps
 */

#pragma once
#include <chrono>
#include <cmath>
#include <cstdint>
#include <toast/log.hpp>
#include <toast/time.hpp>
#include <tracy/Tracy.hpp>

namespace physics {

struct StepResult {
	unsigned steps = 0;
	double alpha = 0.0;
	bool dropped_time = false;

	/// The loop stopped early on max_burst_seconds so a slow tick shrinks its own catch up allowance
	bool time_budget_reached = false;
};

class Accumulator {
public:
	// TODO: This should read from the project settings
	static constexpr double frequency = 60.0;
	static constexpr double fixed_delta = 1.0 / frequency;
	static constexpr unsigned max_steps = 8;

	/// Real time cap on one catch up burst once the first step has already run
	static constexpr double max_burst_seconds = 0.1;

	template<typename Fn>
	auto tick(double dt, Fn&& fn) -> StepResult {
		ZoneScopedN("physics::Accumulator");

		m_accumulator += dt;

		StepResult result;
		const auto burst_start = std::chrono::steady_clock::now();

		while (m_accumulator >= fixed_delta && result.steps < max_steps) {
			if (result.steps > 0) {
				const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - burst_start).count();
				if (elapsed >= max_burst_seconds) {
					result.time_budget_reached = true;
					break;
				}
			}

			fn();

			m_accumulator -= fixed_delta;
			++result.steps;
		}

		if (m_accumulator >= fixed_delta) {
			TOAST_WARN("Physics", "Accumulator overflow: {} seconds of physics time dropped", m_accumulator);
			m_accumulator = std::fmod(m_accumulator, fixed_delta);
			result.dropped_time = true;
		}

		result.alpha = m_accumulator / fixed_delta;
		return result;
	}

	[[nodiscard]]
	auto alpha() const noexcept -> double {
		return m_accumulator / fixed_delta;
	}

	[[nodiscard]]
	auto value() const noexcept -> double {
		return m_accumulator;
	}

	void reset() noexcept { m_accumulator = 0.0; }

private:
	double m_accumulator = 0.0;
};

}
