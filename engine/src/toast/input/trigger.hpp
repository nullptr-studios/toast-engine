/**
 * @file trigger.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Triggers turn a raw input sample into an action value and discrete events
 *
 * There are seven trigger events: start, release, hold, pulse, axis1d, axis2d and cursor
 */

#pragma once

#include "parse_util.hpp"
#include "value.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <string_view>
#include <toml++/toml.hpp>

namespace input {

/**
 * @brief Polymorphic base for all triggers
 */
class TOAST_API ITrigger {
public:
	/**
	 * @brief Outcome of evaluating a trigger for one frame
	 *
	 * A trigger may fire several discrete events in a single frame (for example a start
	 * and an immediate hold), so signals are collected in a small fixed buffer
	 */
	struct Result {
		bool active = false;    ///< whether the trigger contributes its value this frame
		Value value;            ///< value contributed while active
		SignalList signals;     ///< discrete events fired this frame

		void emit(ActionEvent event) noexcept { signals.emit(event); }
	};

	ITrigger() = default;
	ITrigger(const ITrigger&) = default;
	auto operator=(const ITrigger&) -> ITrigger& = default;
	virtual ~ITrigger() = default;

	/**
	 * @brief Evaluates the trigger against the current input sample
	 * @param sample Raw input for the bound keycode this frame
	 * @param ctx Frame context including delta time and viewport metrics
	 * @return The contributed value and any discrete events fired
	 */
	virtual auto evaluate(const InputSample& sample, const EvalContext& ctx) -> Result = 0;

	[[nodiscard]]
	virtual auto name() const -> std::string_view = 0;

	/**
	 * @brief Builds a trigger from a TOML table keyed by its "name" field
	 * @param table The [[trigger]] table
	 * @param vt The value domain of the owning action, used to coerce value fields
	 * @return A new trigger, or nullptr when the name is unknown
	 */
	static auto fromToml(const toml::table& table, ValueType vt) -> std::shared_ptr<ITrigger>;
};

namespace detail {

inline auto levelOf(const InputSample& sample) -> float {
	return sample.is_vector ? glm::length(sample.vector) : std::abs(sample.scalar);
}

inline auto toNDC(glm::vec2 screen, const EvalContext& ctx) -> glm::vec2 {
	const glm::vec2 size = glm::max(ctx.viewport_size, glm::vec2 {1.0f});
	const glm::vec2 local = screen - ctx.viewport_pos;
	return glm::vec2 {(local.x / size.x) * 2.0f - 1.0f, 1.0f - (local.y / size.y) * 2.0f};
}

inline auto deltaToNDC(glm::vec2 screen_delta, const EvalContext& ctx) -> glm::vec2 {
	const glm::vec2 size = glm::max(ctx.viewport_size, glm::vec2 {1.0f});
	return glm::vec2 {(screen_delta.x / size.x) * 2.0f, -(screen_delta.y / size.y) * 2.0f};
}

inline auto applyAxisThreshold(float value, glm::vec2 range, bool normalize) -> float {
	const float mag = std::abs(value);
	if (mag < range.x) {
		return 0.0f;
	}
	if (!normalize) {
		return value;
	}
	const float span = std::max(range.y - range.x, 0.0001f);
	const float t = glm::clamp((mag - range.x) / span, 0.0f, 1.0f);
	return std::copysign(t, value);
}

}

/**
 * @brief Fires once when the input goes from off to on
 */
class StartTrigger : public ITrigger {
public:
	StartTrigger(const toml::table& t, ValueType vt)
	    : m_threshold(parse::number(t["threshold"], 0.5f)),
	      m_value(parse::value(t["value"], vt)),
	      m_countdown(parse::number(t["countdown"], 0.0f)) { }

	auto evaluate(const InputSample& sample, const EvalContext& ctx) -> Result override {
		Result r;
		const bool on = detail::levelOf(sample) >= m_threshold;

		if (on && !m_was_on) {
			m_elapsed = 0.0f;
			if (m_countdown <= 0.0f) {
				m_started = true;
				r.active = true;
				r.value = m_value;
				r.emit(ActionEvent::start);
			} else {
				m_started = false;
				r.emit(ActionEvent::try_);
			}
		} else if (on && !m_started) {
			m_elapsed += ctx.delta;
			if (m_elapsed >= m_countdown) {
				m_started = true;
				r.active = true;
				r.value = m_value;
				r.emit(ActionEvent::start);
			} else {
				r.emit(ActionEvent::countdown);
			}
		} else if (!on && m_was_on) {
			if (!m_started) {
				r.emit(ActionEvent::cancelled);
			}
			m_started = false;
		}

		m_was_on = on;
		return r;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "start";
	}

private:
	float m_threshold = 0.5f;
	Value m_value {true};
	float m_countdown = 0.0f;
	bool m_was_on = false;
	bool m_started = false;
	float m_elapsed = 0.0f;
};

/**
 * @brief Fires once when the input goes from on to off
 */
class ReleaseTrigger : public ITrigger {
public:
	ReleaseTrigger(const toml::table& t, ValueType vt)
	    : m_threshold(parse::number(t["threshold"], 0.5f)),
	      m_value(parse::value(t["value"], vt)) { }

	auto evaluate(const InputSample& sample, const EvalContext& /*ctx*/) -> Result override {
		Result r;
		const bool on = detail::levelOf(sample) >= m_threshold;
		if (!on && m_was_on) {
			r.active = true;
			r.value = m_value;
			r.emit(ActionEvent::release);
		}
		m_was_on = on;
		return r;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "release";
	}

private:
	float m_threshold = 0.5f;
	Value m_value {false};
	bool m_was_on = false;
};

/**
 * @brief Reports a hold every frame while the input is on
 */
class HoldTrigger : public ITrigger {
public:
	HoldTrigger(const toml::table& t, ValueType vt)
	    : m_threshold_start(parse::number(t["threshold_start"], 0.5f)),
	      m_threshold_release(parse::number(t["threshold_release"], 0.5f)),
	      m_value(parse::value(t["value"], vt)),
	      m_send_start(parse::boolean(t["send_start"], false)),
	      m_send_release(parse::boolean(t["send_release"], false)),
	      m_countdown(parse::number(t["countdown"], 0.0f)) { }

	auto evaluate(const InputSample& sample, const EvalContext& ctx) -> Result override {
		Result r;
		const float level = detail::levelOf(sample);

		const bool was_holding = m_holding;
		if (!m_holding && level >= m_threshold_start) {
			m_holding = true;
			m_elapsed = 0.0f;
			m_started = false;
		} else if (m_holding && level < m_threshold_release) {
			m_holding = false;
		}

		if (m_holding) {
			if (!m_started) {
				if (!was_holding && m_countdown <= 0.0f) {
					m_started = true;
				} else {
					m_elapsed += ctx.delta;
					if (m_elapsed >= m_countdown) {
						m_started = true;
					}
				}

				if (m_started) {
					if (m_send_start) {
						r.emit(ActionEvent::start);
					}
				} else {
					r.emit(ActionEvent::countdown);
				}
			}

			if (m_started) {
				r.active = true;
				r.value = m_value;
				r.emit(ActionEvent::hold);
			}
		} else if (was_holding) {
			if (!m_started) {
				r.emit(ActionEvent::cancelled);
			} else if (m_send_release) {
				r.emit(ActionEvent::release);
			}
			m_started = false;
		}

		return r;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "hold";
	}

private:
	float m_threshold_start = 0.5f;
	float m_threshold_release = 0.5f;
	Value m_value {true};
	bool m_send_start = false;
	bool m_send_release = false;
	float m_countdown = 0.0f;
	bool m_holding = false;
	bool m_started = false;
	float m_elapsed = 0.0f;
};

/**
 * @brief Like a hold, but reports a hold once every @c rate seconds instead of every frame
 */
class PulseTrigger : public ITrigger {
public:
	PulseTrigger(const toml::table& t, ValueType vt)
	    : m_threshold_start(parse::number(t["threshold_start"], 0.5f)),
	      m_threshold_release(parse::number(t["threshold_release"], 0.5f)),
	      m_value(parse::value(t["value"], vt)),
	      m_rate(std::max(parse::number(t["rate"], 1.0f), 0.0001f)),
	      m_send_start(parse::boolean(t["send_start"], false)),
	      m_send_release(parse::boolean(t["send_release"], false)),
	      m_countdown(parse::number(t["countdown"], 0.0f)) { }

	auto evaluate(const InputSample& sample, const EvalContext& ctx) -> Result override {
		Result r;
		const float level = detail::levelOf(sample);

		const bool was_holding = m_holding;
		if (!m_holding && level >= m_threshold_start) {
			m_holding = true;
			m_elapsed = 0.0f;
			m_pulse_acc = 0.0f;
			m_started = false;
		} else if (m_holding && level < m_threshold_release) {
			m_holding = false;
		}

		if (m_holding) {
			if (!m_started) {
				if (!was_holding && m_countdown <= 0.0f) {
					m_started = true;
				} else {
					m_elapsed += ctx.delta;
					if (m_elapsed >= m_countdown) {
						m_started = true;
					}
				}

				if (m_started) {
					m_pulse_acc = 0.0f;
					r.active = true;
					r.value = m_value;
					if (m_send_start) {
						r.emit(ActionEvent::start);
					}
					r.emit(ActionEvent::hold);
				} else {
					r.emit(ActionEvent::countdown);
				}
			} else {
				m_pulse_acc += ctx.delta;
				if (m_pulse_acc >= m_rate) {
					m_pulse_acc -= m_rate;
					r.active = true;
					r.value = m_value;
					r.emit(ActionEvent::hold);
				}
			}
		} else if (was_holding) {
			if (!m_started) {
				r.emit(ActionEvent::cancelled);
			} else if (m_send_release) {
				r.emit(ActionEvent::release);
			}
			m_started = false;
		}

		return r;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "pulse";
	}

private:
	float m_threshold_start = 0.5f;
	float m_threshold_release = 0.5f;
	Value m_value {true};
	float m_rate = 1.0f;
	bool m_send_start = false;
	bool m_send_release = false;
	float m_countdown = 0.0f;
	bool m_holding = false;
	bool m_started = false;
	float m_elapsed = 0.0f;
	float m_pulse_acc = 0.0f;
};

/**
 * @brief Continuous 1D axis input
 */
class Axis1DTrigger : public ITrigger {
public:
	Axis1DTrigger(const toml::table& t, ValueType vt)
	    : m_value_type(vt),
	      m_threshold(parse::vec2(t["threshold"], glm::vec2 {0.2f, 1.0f})),
	      m_normalize(parse::boolean(t["normalize"], false)),
	      m_axis(static_cast<int>(parse::number(t["axis"], 0.0f))),
	      m_send_start(parse::boolean(t["send_start"], false)),
	      m_send_release(parse::boolean(t["send_release"], false)) { }

	auto evaluate(const InputSample& sample, const EvalContext& /*ctx*/) -> Result override {
		Result r;
		const float out = detail::applyAxisThreshold(sample.scalar, m_threshold, m_normalize);
		const bool active = std::abs(sample.scalar) >= m_threshold.x;

		if (active) {
			if (m_value_type == ValueType::axis_2d) {
				glm::vec2 v {0.0f};
				v[m_axis == 1 ? 1 : 0] = out;
				r.value = Value {v};
			} else {
				r.value = Value {out};
			}
			r.active = true;
			if (!m_was_active && m_send_start) {
				r.emit(ActionEvent::start);
			}
			r.emit(ActionEvent::hold);
		} else if (m_was_active && m_send_release) {
			r.emit(ActionEvent::release);
		}

		m_was_active = active;
		return r;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "axis1d";
	}

private:
	ValueType m_value_type = ValueType::axis_1d;
	glm::vec2 m_threshold {0.2f, 1.0f};
	bool m_normalize = false;
	int m_axis = 0;
	bool m_send_start = false;
	bool m_send_release = false;
	bool m_was_active = false;
};

/**
 * @brief Continuous 2D axis input
 */
class Axis2DTrigger : public ITrigger {
public:
	Axis2DTrigger(const toml::table& t, ValueType /*vt*/)
	    : m_threshold_x(parse::vec2(t["threshold_x"], glm::vec2 {0.2f, 1.0f})),
	      m_threshold_y(parse::vec2(t["threshold_y"], glm::vec2 {0.2f, 1.0f})),
	      m_normalize(parse::boolean(t["normalize"], false)),
	      m_send_start(parse::boolean(t["send_start"], false)),
	      m_send_release(parse::boolean(t["send_release"], false)) { }

	auto evaluate(const InputSample& sample, const EvalContext& /*ctx*/) -> Result override {
		Result r;
		const glm::vec2 out {
		  detail::applyAxisThreshold(sample.vector.x, m_threshold_x, m_normalize),
		  detail::applyAxisThreshold(sample.vector.y, m_threshold_y, m_normalize),
		};
		const bool active = out.x != 0.0f || out.y != 0.0f;

		if (active) {
			r.active = true;
			r.value = Value {out};
			if (!m_was_active && m_send_start) {
				r.emit(ActionEvent::start);
			}
			r.emit(ActionEvent::hold);
		} else if (m_was_active && m_send_release) {
			r.emit(ActionEvent::release);
		}

		m_was_active = active;
		return r;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "axis2d";
	}

private:
	glm::vec2 m_threshold_x {0.2f, 1.0f};
	glm::vec2 m_threshold_y {0.2f, 1.0f};
	bool m_normalize = false;
	bool m_send_start = false;
	bool m_send_release = false;
	bool m_was_active = false;
};

/**
 * @brief Mouse cursor position or delta
 */
class CursorTrigger : public ITrigger {
	// TODO: Camera and world space fall back to NDC until renderer camera access is wired

public:
	CursorTrigger(const toml::table& t, ValueType /*vt*/)
	    : m_space(static_cast<int>(parse::number(t["space"], 1.0f))),
	      m_delta(parse::boolean(t["delta"], false)) { }

	auto evaluate(const InputSample& sample, const EvalContext& ctx) -> Result override {
		Result r;
		const glm::vec2 screen = sample.vector;

		glm::vec2 measured = screen;
		if (m_delta) {
			measured = m_has_last ? (screen - m_last) : glm::vec2 {0.0f};
			m_last = screen;
			m_has_last = true;
		}

		glm::vec2 out;
		if (m_space == 0) {
			out = measured;    // screen space: raw pixels
		} else {
			// TODO: implement camera (2) and world (3) space projection once camera access is wired
			out = m_delta ? detail::deltaToNDC(measured, ctx) : detail::toNDC(measured, ctx);
		}

		r.active = true;
		r.value = Value {out};
		r.emit(ActionEvent::hold);
		return r;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "cursor";
	}

private:
	int m_space = 1;
	bool m_delta = false;
	glm::vec2 m_last {0.0f};
	bool m_has_last = false;
};

}
