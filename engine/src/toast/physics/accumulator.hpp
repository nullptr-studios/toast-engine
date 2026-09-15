/**
 * @file Accumulator.hpp
 * @author Xein
 * @date 09 Sep 2026
 * @brief Handles accumulating the physics steps
 */

#pragma once
#include <cmath>
#include <cstdint>
#include <toast/log.hpp>
#include <toast/time.hpp>

namespace physics {

struct StepResult {
	unsigned steps = 0;
	double alpha = 0.0;
	bool dropped_time = false;
};

class Accumulator {
public:
	// TODO: This should read from the project settings
	static constexpr double frequency = 60.0;
	static constexpr double fixed_delta = 1.0 / frequency;
	static constexpr unsigned max_steps = 8;

	template<typename Fn>
	auto tick(double dt, Fn&& fn) -> StepResult {
		ZoneScoped;

		m_accumulator += dt;

		StepResult result;

		while (m_accumulator >= fixed_delta && result.steps < max_steps) {
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
