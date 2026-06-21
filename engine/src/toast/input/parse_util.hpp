/**
 * @file parse_util.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Small helpers shared by the TOML parsing paths of triggers, modifiers and binds
 */

#pragma once

#include "value.hpp"

#include <glm/glm.hpp>
#include <toml++/toml.hpp>

namespace input::parse {

inline auto vec2(const toml::node_view<const toml::node>& node, glm::vec2 fallback) -> glm::vec2 {
	if (const auto* arr = node.as_array()) {
		glm::vec2 out = fallback;
		if (arr->size() >= 1) {
			out.x = static_cast<float>((*arr)[0].value_or(static_cast<double>(fallback.x)));
		}
		if (arr->size() >= 2) {
			out.y = static_cast<float>((*arr)[1].value_or(static_cast<double>(fallback.y)));
		}
		return out;
	}
	if (auto scalar = node.value<double>()) {
		return glm::vec2 {static_cast<float>(*scalar)};
	}
	return fallback;
}

inline auto number(const toml::node_view<const toml::node>& node, float fallback) -> float {
	return static_cast<float>(node.value_or(static_cast<double>(fallback)));
}

inline auto boolean(const toml::node_view<const toml::node>& node, bool fallback) -> bool {
	return node.value_or(fallback);
}

inline auto value(const toml::node_view<const toml::node>& node, ValueType vt) -> Value {
	switch (vt) {
		case ValueType::axis_0d: return Value {boolean(node, true), ValueType::axis_0d};
		case ValueType::axis_1d: return Value {number(node, 1.0f), ValueType::axis_1d};
		case ValueType::axis_2d: return Value {vec2(node, glm::vec2 {1.0f, 0.0f}), ValueType::axis_2d};
	}
	return Value {};
}

}
