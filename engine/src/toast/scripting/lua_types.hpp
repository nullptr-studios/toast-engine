/**
 * @file lua_types.hpp
 * @author Xein
 * @date 13 Jul 2026
 *
 * @brief Small value types registered as Lua usertypes
 */

#pragma once

#include <cstdint>
#include <format>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

namespace scripting {

/**
 * @brief Lua-side placeholder declaring a typed Node or Asset reference
 */
struct TypeMarker {
	enum class Kind : uint8_t {
		node,
		asset
	} kind;
	std::string type_name;

	[[nodiscard]]
	auto toString() const -> std::string {
		if (kind == Kind::node) {
			return std::format("NodeType({})", type_name.empty() ? "any" : type_name);
		}
		return std::format("AssetType({})", type_name.empty() ? "any" : type_name);
	}
};

/**
 * @brief RGB color usertype
 *
 * I did it like this so we can have the Color variables on lua be a color picker
 * on the inspector UI
 */
struct Color3 {
	glm::vec3 rgb {0.0f};

	Color3() = default;

	Color3(float r, float g, float b) : rgb(r, g, b) { }

	explicit Color3(const glm::vec3& v) : rgb(v) { }

	[[nodiscard]]
	auto toString() const -> std::string {
		return std::format("color3({}, {}, {})", rgb.r, rgb.g, rgb.b);
	}
};

// Variant of Color3 but with alpha channel
struct Color4 {
	glm::vec4 rgba {0.0f};

	Color4() = default;

	Color4(float r, float g, float b, float a) : rgba(r, g, b, a) { }

	explicit Color4(const glm::vec4& v) : rgba(v) { }

	[[nodiscard]]
	auto toString() const -> std::string {
		return std::format("color4({}, {}, {}, {})", rgba.r, rgba.g, rgba.b, rgba.a);
	}
};

}
