/**
 * @file modifier.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Modifiers post-process a value in place before it reaches the action
 */

#pragma once

#include "parse_util.hpp"
#include "toast/time.hpp"
#include "value.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <string_view>
#include <toml++/toml.hpp>

namespace input {

/**
 * @brief Polymorphic base for all modifiers
 */
class TOAST_API IModifier {
public:
	IModifier() = default;
	IModifier(const IModifier&) = default;
	auto operator=(const IModifier&) -> IModifier& = default;
	virtual ~IModifier() = default;

	/**
	 * @brief Transforms a value in place
	 * @param value The value to modify; its type tag is preserved
	 * @param ctx Frame context, used by the key and delta-time modifiers
	 */
	virtual void apply(Value& value, const EvalContext& ctx) = 0;

	/// @return The modifier's TOML name
	[[nodiscard]]
	virtual auto name() const -> std::string_view = 0;

	/**
	 * @brief Builds a modifier from a TOML table keyed by its "name" field
	 * @param table The [[modifier]] table
	 * @return A new modifier, or nullptr when the name is unknown
	 */
	static auto fromToml(const toml::table& table) -> std::shared_ptr<IModifier>;
};

namespace detail {

inline auto modifierHeld(ModifierKey mask, ModifierKey key) -> bool {
	return (mask & key) == key;
}

}

/**
 * @brief Zeroes input below a lower threshold and remaps the rest to [0, 1]
 */
class DeadzoneModifier : public IModifier {
public:
	explicit DeadzoneModifier(const toml::table& t)
	    : m_lower(parse::number(t["lower_threshold"], 0.2f)),
	      m_upper(parse::number(t["upper_threshold"], 1.0f)) { }

	void apply(Value& value, const EvalContext& /*ctx*/) override {
		value.data.x = remap(value.data.x);
		value.data.y = remap(value.data.y);
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "deadzone";
	}

private:
	[[nodiscard]]
	auto remap(float v) const -> float {
		const float mag = std::abs(v);
		if (mag < m_lower) {
			return 0.0f;
		}
		const float span = std::max(m_upper - m_lower, 0.0001f);
		const float t = glm::clamp((mag - m_lower) / span, 0.0f, 1.0f);
		return std::copysign(t, v);
	}

	float m_lower = 0.2f;
	float m_upper = 1.0f;
};

/**
 * @brief Negates the configured axes
 */
class InvertModifier : public IModifier {
public:
	explicit InvertModifier(const toml::table& t) : m_x(parse::boolean(t["x"], true)), m_y(parse::boolean(t["y"], false)) { }

	void apply(Value& value, const EvalContext& /*ctx*/) override {
		if (m_x) {
			value.data.x *= -1.0f;
		}
		if (m_y) {
			value.data.y *= -1.0f;
		}
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "invert";
	}

private:
	bool m_x = true;
	bool m_y = false;
};

/**
 * @brief Converts a linear value to an exponential curve, preserving sign
 */
class ExponentialModifier : public IModifier {
public:
	explicit ExponentialModifier(const toml::table& t) : m_power(parse::number(t["power"], 2.0f)) { }

	void apply(Value& value, const EvalContext& /*ctx*/) override {
		value.data.x = curve(value.data.x);
		value.data.y = curve(value.data.y);
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "exponential";
	}

private:
	[[nodiscard]]
	auto curve(float v) const -> float {
		return std::copysign(std::pow(std::abs(v), m_power), v);
	}

	float m_power = 2.0f;
};

/**
 * @brief Multiplies each axis by a constant
 */
class ScaleModifier : public IModifier {
public:
	explicit ScaleModifier(const toml::table& t) : m_x(parse::number(t["x"], 1.0f)), m_y(parse::number(t["y"], 1.0f)) { }

	void apply(Value& value, const EvalContext& /*ctx*/) override {
		value.data.x *= m_x;
		value.data.y *= m_y;
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "scale";
	}

private:
	float m_x = 1.0f;
	float m_y = 1.0f;
};

/**
 * @brief Multiplies each axis by the frame delta time
 */
class ScaleByDeltaTimeModifier : public IModifier {
public:
	explicit ScaleByDeltaTimeModifier(const toml::table& t)
	    : m_x(parse::boolean(t["x"], true)),
	      m_y(parse::boolean(t["y"], true)) { }

	void apply(Value& value, const EvalContext& /*ctx*/) override {
		float delta = Time::delta();
		if (m_x) {
			value.data.x *= delta;
		}
		if (m_y) {
			value.data.y *= delta;
		}
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "scale_by_deltatime";
	}

private:
	bool m_x = true;
	bool m_y = true;
};

/**
 * @brief Smooths each axis toward its target with a lerp
 */
class SmoothModifier : public IModifier {
public:
	explicit SmoothModifier(const toml::table& t)
	    : m_x(parse::boolean(t["x"], true)),
	      m_y(parse::boolean(t["y"], true)),
	      m_speed(parse::number(t["speed"], 10.0f)) { }

	void apply(Value& value, const EvalContext& /*ctx*/) override {
		float delta = Time::delta();
		const float t = glm::clamp(m_speed * delta, 0.0f, 1.0f);
		if (m_x) {
			m_current.x = glm::mix(m_current.x, value.data.x, t);
			value.data.x = m_current.x;
		}
		if (m_y) {
			m_current.y = glm::mix(m_current.y, value.data.y, t);
			value.data.y = m_current.y;
		}
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "smooth";
	}

private:
	bool m_x = true;
	bool m_y = true;
	float m_speed = 10.0f;
	glm::vec2 m_current {0.0f};
};

/**
 * @brief Zeroes it unless every required keyboard modifier is held
 */
class KeyModifier : public IModifier {
public:
	explicit KeyModifier(const toml::table& t)
	    : m_control(parse::boolean(t["control"], false)),
	      m_shift(parse::boolean(t["shift"], false)),
	      m_alt(parse::boolean(t["alt"], false)) { }

	void apply(Value& value, const EvalContext& ctx) override {
		const bool ok = (!m_control || detail::modifierHeld(ctx.mods, ModifierKey::control)) &&
		                (!m_shift || detail::modifierHeld(ctx.mods, ModifierKey::shift)) &&
		                (!m_alt || detail::modifierHeld(ctx.mods, ModifierKey::alt));
		if (!ok) {
			value.data = glm::vec2 {0.0f};
		}
	}

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "key";
	}

private:
	bool m_control = false;
	bool m_shift = false;
	bool m_alt = false;
};

}
