/**
 * @file keycodes.hpp
 * @author Xein
 * @date 21 Jun 2026
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <toast/export.hpp>

namespace input {

/**
 * @brief Physical device a keycode belongs to
 */
enum class Device : uint8_t {
	none,
	keyboard,
	mouse,
	controller,
};

/**
 * @brief Bit flags for the keyboard modifier keys held during an input
 */
enum class ModifierKey : uint8_t {
	none = 0,
	shift = 0b001,
	control = 0b010,
	alt = 0b100,
};

[[nodiscard]]
constexpr auto operator|(ModifierKey a, ModifierKey b) -> ModifierKey {
	return static_cast<ModifierKey>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]]
constexpr auto operator&(ModifierKey a, ModifierKey b) -> ModifierKey {
	return static_cast<ModifierKey>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

/**
 * @brief Dimensionality of an input, used by the input system to pick a sampling path
 */
enum class InputKind : uint8_t {
	button,    ///< on/off input such as a key or a gamepad button
	axis1d,    ///< single-axis analog input such as a trigger or one stick axis
	axis2d,    ///< two-axis analog input such as a full stick or the touchpad
	scroll,    ///< mouse wheel; code 0 is vertical, code 1 is horizontal
	cursor,    ///< mouse cursor position or delta
};

/**
 * @brief A parsed keycode resolved to a device, kind and numeric code
 *
 * The meaning of @ref code depends on the device and kind: an SDL keycode for the
 * keyboard, an SDL mouse button, an SDL gamepad button or axis, or a stick index for
 * a 2D controller axis (0 left stick, 1 right stick, 2 touchpad)
 */
struct KeyCode {
	Device device = Device::none;
	InputKind kind = InputKind::button;
	int code = 0;
	bool valid = false;
};

/// @brief Controller stick indices used as the code for InputKind::axis2d controller inputs
constexpr int controller_stick_left = 0;
constexpr int controller_stick_right = 1;
constexpr int controller_touchpad = 2;

/// @brief Gyro axis codes used as the code for the controller gyro_x/y/z inputs
constexpr int controller_gyro_x = 100;
constexpr int controller_gyro_y = 101;
constexpr int controller_gyro_z = 102;

/**
 * @brief Parses a textual keycode such as "keyboard/a", "mouse/left" or "controller/left_stick"
 * @param str The keycode string; the device prefix selects the parsing path
 * @return A KeyCode with valid set to true on success, or valid false when the string is unknown
 */
[[nodiscard]]
auto TOAST_API parseKeycode(std::string_view str) -> KeyCode;

}
