/**
 * @file keycodes.cpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Implementation of the keycode parser
 */

#include "keycodes.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <toast/log.hpp>
#include <unordered_map>

namespace input {

namespace {

auto normalize(std::string_view raw) -> std::string {
	std::string s(raw);
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch) == 0; }));
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return std::isspace(ch) == 0; }).base(), s.end());
	std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	for (char& c : s) {
		if (c == ' ' || c == '-') {
			c = '_';
		}
	}
	return s;
}

auto toInt(std::string_view s) -> std::optional<int> {
	int value = 0;
	const auto* end = s.data() + s.size();
	auto [ptr, ec] = std::from_chars(s.data(), end, value);
	if (ec != std::errc {} || ptr != end) {
		return std::nullopt;
	}
	return value;
}

auto parseKeyboard(std::string_view name) -> KeyCode {
	const std::string n = normalize(name);
	KeyCode key {.device = Device::keyboard, .kind = InputKind::button};

	// let SDL resolve letters, digits and raw symbols
	if (n.size() == 1) {
		key.code = static_cast<int>(SDL_GetKeyFromName(n.c_str()));
		key.valid = key.code != SDLK_UNKNOWN;
		return key;
	}

	// Function keys f1 through f24
	if (n.size() >= 2 && n[0] == 'f') {
		if (auto idx = toInt(std::string_view(n).substr(1)); idx && *idx >= 1 && *idx <= 24) {
			key.code = SDLK_F1 + (*idx - 1);
			key.valid = true;
			return key;
		}
	}

	// clang-format off
	static const std::unordered_map<std::string, SDL_Keycode> map {
	  {	      "space",        SDLK_SPACE},
	  {	      "enter",       SDLK_RETURN},
	  {	     "return",       SDLK_RETURN},
	  {	        "tab",          SDLK_TAB},
	  {    "backspace",    SDLK_BACKSPACE},
	  {	     "delete",       SDLK_DELETE},
	  {	     "insert",       SDLK_INSERT},
	  {	     "escape",       SDLK_ESCAPE},
	  {	        "esc",       SDLK_ESCAPE},
	  {	       "home",         SDLK_HOME},
	  {	        "end",          SDLK_END},
	  {	    "page_up",       SDLK_PAGEUP},
	  {    "page_down",     SDLK_PAGEDOWN},
	  {	         "up",	         SDLK_UP},
	  {	       "down",         SDLK_DOWN},
	  {	       "left",         SDLK_LEFT},
	  {	      "right",        SDLK_RIGHT},
	  {	       "caps",     SDLK_CAPSLOCK},
	  {    "caps_lock",     SDLK_CAPSLOCK},
	  {   "left_shift",       SDLK_LSHIFT},
	  {  "right_shift",       SDLK_RSHIFT},
	  { "left_control",        SDLK_LCTRL},
	  {"right_control",        SDLK_RCTRL},
	  {	   "left_alt",         SDLK_LALT},
	  {    "right_alt",         SDLK_RALT},
	  {   "left_super",         SDLK_LGUI},
	  {  "right_super",         SDLK_RGUI},
	  { "left_bracket",  SDLK_LEFTBRACKET},
	  {"right_bracket", SDLK_RIGHTBRACKET},
	  {    "backslash",    SDLK_BACKSLASH},
	  {    "semicolon",    SDLK_SEMICOLON},
	  {   "apostrophe",   SDLK_APOSTROPHE},
	  {	      "comma",        SDLK_COMMA},
	  {	     "period",       SDLK_PERIOD},
	  {	      "slash",        SDLK_SLASH},
	  {	      "minus",        SDLK_MINUS},
	  {	      "equal",       SDLK_EQUALS},
	  {	      "grave",        SDLK_GRAVE},
	};
	// clang-format on

	if (auto it = map.find(n); it != map.end()) {
		key.code = static_cast<int>(it->second);
		key.valid = true;
		return key;
	}

	const SDL_Keycode by_name = SDL_GetKeyFromName(std::string(name).c_str());
	key.code = static_cast<int>(by_name);
	key.valid = by_name != SDLK_UNKNOWN;
	return key;
}

auto parseMouse(std::string_view name) -> KeyCode {
	const std::string n = normalize(name);
	KeyCode key {.device = Device::mouse};

	if (n == "left") {
		key.kind = InputKind::button;
		key.code = SDL_BUTTON_LEFT;
		key.valid = true;
	} else if (n == "right") {
		key.kind = InputKind::button;
		key.code = SDL_BUTTON_RIGHT;
		key.valid = true;
	} else if (n == "center" || n == "middle") {
		key.kind = InputKind::button;
		key.code = SDL_BUTTON_MIDDLE;
		key.valid = true;
	} else if (n == "scroll") {
		key.kind = InputKind::scroll;
		key.code = 0;    // vertical
		key.valid = true;
	} else if (n == "horizontal_scroll") {
		key.kind = InputKind::scroll;
		key.code = 1;    // horizontal
		key.valid = true;
	} else if (n == "cursor") {
		key.kind = InputKind::cursor;
		key.code = 0;
		key.valid = true;
	} else if (n.rfind("button_", 0) == 0) {
		if (auto idx = toInt(std::string_view(n).substr(7)); idx && *idx >= 1 && *idx <= SDL_BUTTON_X2) {
			key.kind = InputKind::button;
			key.code = *idx;
			key.valid = true;
		}
	}

	return key;
}

auto parseController(std::string_view name) -> KeyCode {
	const std::string n = normalize(name);
	KeyCode key {.device = Device::controller};

	auto button = [&](int code) {
		key.kind = InputKind::button;
		key.code = code;
		key.valid = true;
	};
	auto axis1d = [&](int code) {
		key.kind = InputKind::axis1d;
		key.code = code;
		key.valid = true;
	};
	auto axis2d = [&](int code) {
		key.kind = InputKind::axis2d;
		key.code = code;
		key.valid = true;
	};

	// Face buttons
	if (n == "a") {
		button(SDL_GAMEPAD_BUTTON_SOUTH);
	} else if (n == "b") {
		button(SDL_GAMEPAD_BUTTON_EAST);
	} else if (n == "x") {
		button(SDL_GAMEPAD_BUTTON_WEST);
	} else if (n == "y") {
		button(SDL_GAMEPAD_BUTTON_NORTH);
	}
	// D-pad
	else if (n == "up") {
		button(SDL_GAMEPAD_BUTTON_DPAD_UP);
	} else if (n == "down") {
		button(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
	} else if (n == "left") {
		button(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
	} else if (n == "right") {
		button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
	}
	// Menu buttons
	else if (n == "start") {
		button(SDL_GAMEPAD_BUTTON_START);
	} else if (n == "select") {
		button(SDL_GAMEPAD_BUTTON_BACK);
	}
	// Sticks
	else if (n == "left_stick") {
		axis2d(controller_stick_left);
	} else if (n == "right_stick") {
		axis2d(controller_stick_right);
	} else if (n == "left_stick_x") {
		axis1d(SDL_GAMEPAD_AXIS_LEFTX);
	} else if (n == "left_stick_y") {
		axis1d(SDL_GAMEPAD_AXIS_LEFTY);
	} else if (n == "right_stick_x") {
		axis1d(SDL_GAMEPAD_AXIS_RIGHTX);
	} else if (n == "right_stick_y") {
		axis1d(SDL_GAMEPAD_AXIS_RIGHTY);
	}
	// Shoulders, triggers, stick clicks and back paddles
	else if (n == "left_shoulder") {
		button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
	} else if (n == "left_trigger") {
		axis1d(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
	} else if (n == "left_stick_press") {
		button(SDL_GAMEPAD_BUTTON_LEFT_STICK);
	} else if (n == "left_4") {
		button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1);
	} else if (n == "left_5") {
		button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2);
	} else if (n == "right_shoulder") {
		button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
	} else if (n == "right_trigger") {
		axis1d(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
	} else if (n == "right_stick_press") {
		button(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
	} else if (n == "right_4") {
		button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);
	} else if (n == "right_5") {
		button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2);
	}
	// Touchpad
	else if (n == "touchpad") {
		axis2d(controller_touchpad);
	} else if (n == "touchpad_click") {
		button(SDL_GAMEPAD_BUTTON_TOUCHPAD);
	}
	// Gyro
	else if (n == "gyro_x") {
		axis1d(controller_gyro_x);
	} else if (n == "gyro_y") {
		axis1d(controller_gyro_y);
	} else if (n == "gyro_z") {
		axis1d(controller_gyro_z);
	}

	return key;
}

}

auto parseKeycode(std::string_view str) -> KeyCode {
	const std::string lowered = normalize(str);

	constexpr std::string_view keyboard_prefix = "keyboard/";
	constexpr std::string_view mouse_prefix = "mouse/";
	constexpr std::string_view controller_prefix = "controller/";

	if (lowered.rfind(keyboard_prefix, 0) == 0) {
		return parseKeyboard(std::string_view(lowered).substr(keyboard_prefix.size()));
	}
	if (lowered.rfind(mouse_prefix, 0) == 0) {
		return parseMouse(std::string_view(lowered).substr(mouse_prefix.size()));
	}
	if (lowered.rfind(controller_prefix, 0) == 0) {
		KeyCode key = parseController(std::string_view(lowered).substr(controller_prefix.size()));
		if (!key.valid) {
			TOAST_WARN("Input", "Unrecognized keycode '{}'", str);
		}
		return key;
	}

	// No recognized prefix
	KeyCode key = parseKeyboard(lowered);
	if (!key.valid) {
		TOAST_WARN("Input", "Unrecognized keycode '{}'", str);
	}
	return key;
}

}
