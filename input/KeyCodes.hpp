/// @file KeyCodes.hpp
/// @date 13 Dec 2025
/// @author Xein

#pragma once

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>

namespace input {

enum class ModifierKey : uint8_t {
	None = 0,
	Shift = 0b001,
	Control = 0b010,
	Alt = 0b100
};

enum class Device : uint8_t {
	Null = 0,
	Keyboard = 0b00001,
	Mouse = 0b00010,
	ControllerButton = 0b00100,
	ControllerAxis = 0b01000,
	ControllerStick = 0b10000
};

constexpr unsigned MOUSE_POSITION_CODE = -1;
constexpr unsigned MOUSE_SCROLL_X_CODE = -2;
constexpr unsigned MOUSE_SCROLL_Y_CODE = -3;
constexpr unsigned MOUSE_RAW_CODE = -4;
constexpr unsigned MOUSE_DELTA_CODE = -5;

static void trim_inplace(std::string& s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

static std::string to_lower_copy(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

static std::string normalize_name(std::string s) {
	trim_inplace(s);
	s = to_lower_copy(s);
	for (char& c : s) {
		if (c == ' ' || c == '-') {
			c = '_';
		}
	}
	return s;
}

inline auto KeyboardKeycodeFromString(std::string& str) -> std::optional<int>;
inline auto MouseKeycodeFromString(std::string& str) -> std::optional<int>;
inline auto ControllerButtonFromString(std::string& str) -> std::optional<int>;
inline auto ControllerAxisFromString(std::string& str) -> std::optional<int>;
inline auto ControllerStickFromString(std::string& str) -> std::optional<int>;

inline auto KeycodeFromString(std::string& str) -> std::optional<std::pair<unsigned, Device>> {
	str = to_lower_copy(str);

	if (str.rfind("mouse/", 0) == 0) {
		auto result = MouseKeycodeFromString(str);
		if (!result.has_value()) {
			return std::nullopt;
		}
		return std::make_pair(static_cast<unsigned>(result.value()), Device::Mouse);
	}

	if (str.rfind("controller/button/", 0) == 0) {
		auto result = ControllerButtonFromString(str);
		if (!result.has_value()) {
			return std::nullopt;
		}
		return std::make_pair(static_cast<unsigned>(result.value()), Device::ControllerButton);
	}

	if (str.rfind("controller/axis/", 0) == 0) {
		auto result = ControllerAxisFromString(str);
		if (!result.has_value()) {
			return std::nullopt;
		}
		return std::make_pair(static_cast<unsigned>(result.value()), Device::ControllerAxis);
	}

	if (str.rfind("controller/stick/", 0) == 0) {
		auto result = ControllerStickFromString(str);
		if (!result.has_value()) {
			return std::nullopt;
		}
		return std::make_pair(static_cast<unsigned>(result.value()), Device::ControllerStick);
	}

	auto result = KeyboardKeycodeFromString(str);
	if (!result.has_value()) {
		return std::nullopt;
	}
	return std::make_pair(static_cast<unsigned>(result.value()), Device::Keyboard);
}

inline auto KeyboardKeycodeFromString(std::string& str) -> std::optional<int> {
	trim_inplace(str);
	if (str.empty()) {
		return std::nullopt;
	}

	std::string n = normalize_name(str);

	if (n.rfind("numpad/", 0) == 0) {
		std::string key = n.substr(7);
		if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
			return SDLK_KP_0 + (key[0] - '0');
		}
		if (key == "decimal" || key == "." || key == "dot") {
			return SDLK_KP_PERIOD;
		}
		if (key == "enter" || key == "return") {
			return SDLK_KP_ENTER;
		}
		if (key == "add" || key == "+" || key == "plus") {
			return SDLK_KP_PLUS;
		}
		if (key == "subtract" || key == "sub" || key == "-" || key == "minus") {
			return SDLK_KP_MINUS;
		}
		if (key == "multiply" || key == "mul" || key == "*" || key == "asterisk") {
			return SDLK_KP_MULTIPLY;
		}
		if (key == "divide" || key == "div" || key == "/") {
			return SDLK_KP_DIVIDE;
		}
		if (key == "equal" || key == "=") {
			return SDLK_KP_EQUALS;
		}
		return std::nullopt;
	}

	if (str.size() == 1) {
		return static_cast<int>(SDL_GetKeyFromName(str.c_str()));
	}

	if (n.size() >= 2 && n[0] == 'f') {
		try {
			const int idx = std::stoi(n.substr(1));
			if (idx >= 1 && idx <= 24) {
				return SDLK_F1 + (idx - 1);
			}
		} catch (...) { }
	}

	static const std::unordered_map<std::string, SDL_Keycode> map {
	  {	      "space",        SDLK_SPACE},
	  {	      "enter",       SDLK_RETURN},
	  {	     "return",       SDLK_RETURN},
	  {	        "tab",          SDLK_TAB},
	  {    "backspace",    SDLK_BACKSPACE},
	  {	     "escape",       SDLK_ESCAPE},
	  {	        "esc",       SDLK_ESCAPE},
	  {	     "insert",       SDLK_INSERT},
	  {	     "delete",       SDLK_DELETE},
	  {	       "home",         SDLK_HOME},
	  {	        "end",          SDLK_END},
	  {	    "page_up",       SDLK_PAGEUP},
	  {    "page_down",     SDLK_PAGEDOWN},
	  {	         "up",	         SDLK_UP},
	  {	       "down",         SDLK_DOWN},
	  {	       "left",         SDLK_LEFT},
	  {	      "right",        SDLK_RIGHT},
	  {   "left_shift",       SDLK_LSHIFT},
	  {  "right_shift",       SDLK_RSHIFT},
	  { "left_control",        SDLK_LCTRL},
	  {"right_control",        SDLK_RCTRL},
	  {    "left_ctrl",        SDLK_LCTRL},
	  {   "right_ctrl",        SDLK_RCTRL},
	  {	   "left_alt",         SDLK_LALT},
	  {    "right_alt",         SDLK_RALT},
	  {   "left_super",         SDLK_LGUI},
	  {  "right_super",         SDLK_RGUI},
	  {    "caps_lock",     SDLK_CAPSLOCK},
	  {  "scroll_lock",   SDLK_SCROLLLOCK},
	  {	   "num_lock", SDLK_NUMLOCKCLEAR},
	  {	       "menu",         SDLK_MENU},
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
	  { "grave_accent",        SDLK_GRAVE},
	  {	       "kp_0",         SDLK_KP_0},
	  {	       "kp_1",         SDLK_KP_1},
	  {	       "kp_2",         SDLK_KP_2},
	  {	       "kp_3",         SDLK_KP_3},
	  {	       "kp_4",         SDLK_KP_4},
	  {	       "kp_5",         SDLK_KP_5},
	  {	       "kp_6",         SDLK_KP_6},
	  {	       "kp_7",         SDLK_KP_7},
	  {	       "kp_8",         SDLK_KP_8},
	  {	       "kp_9",         SDLK_KP_9},
	  {   "kp_decimal",    SDLK_KP_PERIOD},
	  {	   "kp_enter",     SDLK_KP_ENTER},
	  {	     "kp_add",      SDLK_KP_PLUS},
	  {  "kp_subtract",     SDLK_KP_MINUS},
	  {  "kp_multiply",  SDLK_KP_MULTIPLY},
	  {    "kp_divide",    SDLK_KP_DIVIDE},
	  {	   "kp_equal",    SDLK_KP_EQUALS},
	  { "print_screen",  SDLK_PRINTSCREEN},
	  {	      "pause",        SDLK_PAUSE},
	  {  "pause_break",        SDLK_PAUSE},
	};

	auto it = map.find(n);
	if (it != map.end()) {
		return static_cast<int>(it->second);
	}

	const SDL_Keycode by_name = SDL_GetKeyFromName(str.c_str());
	if (by_name != SDLK_UNKNOWN) {
		return static_cast<int>(by_name);
	}

	return std::nullopt;
}

inline auto MouseKeycodeFromString(std::string& str) -> std::optional<int> {
	std::string key = normalize_name(str);
	if (key.rfind("mouse/", 0) != 0) {
		return std::nullopt;
	}
	key = key.substr(6);

	if (key == "position" || key == "pos") {
		return static_cast<int>(MOUSE_POSITION_CODE);
	}
	if (key == "scrollx" || key == "x") {
		return static_cast<int>(MOUSE_SCROLL_X_CODE);
	}
	if (key == "scrolly" || key == "scroll" || key == "y") {
		return static_cast<int>(MOUSE_SCROLL_Y_CODE);
	}
	if (key == "raw") {
		return static_cast<int>(MOUSE_RAW_CODE);
	}
	if (key == "delta") {
		return static_cast<int>(MOUSE_DELTA_CODE);
	}

	if (key == "left" || key == "btn_left" || key == "l") {
		return SDL_BUTTON_LEFT;
	}
	if (key == "right" || key == "btn_right" || key == "r") {
		return SDL_BUTTON_RIGHT;
	}
	if (key == "middle" || key == "wheel" || key == "btn_middle" || key == "m") {
		return SDL_BUTTON_MIDDLE;
	}

	if (key.rfind("button", 0) == 0) {
		key = key.substr(6);
	}
	if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit)) {
		const int idx = std::atoi(key.c_str());
		if (idx >= 1 && idx <= SDL_BUTTON_X2) {
			return idx;
		}
	}

	return std::nullopt;
}

inline auto ControllerButtonFromString(std::string& str) -> std::optional<int> {
	std::string key = normalize_name(str);
	if (key.rfind("controller/button/", 0) != 0) {
		return std::nullopt;
	}
	key = key.substr(18);

	if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit)) {
		const int idx = std::atoi(key.c_str());
		if (idx >= 0 && idx < SDL_GAMEPAD_BUTTON_COUNT) {
			return idx;
		}
	}

	if (key == "a" || key == "cross") {
		return SDL_GAMEPAD_BUTTON_SOUTH;
	}
	if (key == "b" || key == "circle") {
		return SDL_GAMEPAD_BUTTON_EAST;
	}
	if (key == "x" || key == "square") {
		return SDL_GAMEPAD_BUTTON_WEST;
	}
	if (key == "y" || key == "triangle") {
		return SDL_GAMEPAD_BUTTON_NORTH;
	}
	if (key == "left_bumper" || key == "lb" || key == "left_shoulder") {
		return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
	}
	if (key == "right_bumper" || key == "rb" || key == "right_shoulder") {
		return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
	}
	if (key == "back" || key == "select") {
		return SDL_GAMEPAD_BUTTON_BACK;
	}
	if (key == "guide" || key == "home") {
		return SDL_GAMEPAD_BUTTON_GUIDE;
	}
	if (key == "start") {
		return SDL_GAMEPAD_BUTTON_START;
	}
	if (key == "left_thumb" || key == "left_stick" || key == "ls") {
		return SDL_GAMEPAD_BUTTON_LEFT_STICK;
	}
	if (key == "right_thumb" || key == "right_stick" || key == "rs") {
		return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
	}
	if (key == "dpad_up" || key == "up") {
		return SDL_GAMEPAD_BUTTON_DPAD_UP;
	}
	if (key == "dpad_right" || key == "right") {
		return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
	}
	if (key == "dpad_down" || key == "down") {
		return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	}
	if (key == "dpad_left" || key == "left") {
		return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
	}

	return std::nullopt;
}

inline auto ControllerAxisFromString(std::string& str) -> std::optional<int> {
	std::string key = normalize_name(str);
	if (key.rfind("controller/axis/", 0) != 0) {
		return std::nullopt;
	}
	key = key.substr(16);

	if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit)) {
		const int idx = std::atoi(key.c_str());
		if (idx >= 0 && idx < SDL_GAMEPAD_AXIS_COUNT) {
			return idx;
		}
	}

	if (key == "left_x" || key == "lx" || key == "left_stick_x") {
		return SDL_GAMEPAD_AXIS_LEFTX;
	}
	if (key == "left_y" || key == "ly" || key == "left_stick_y") {
		return SDL_GAMEPAD_AXIS_LEFTY;
	}
	if (key == "right_x" || key == "rx" || key == "right_stick_x") {
		return SDL_GAMEPAD_AXIS_RIGHTX;
	}
	if (key == "right_y" || key == "ry" || key == "right_stick_y") {
		return SDL_GAMEPAD_AXIS_RIGHTY;
	}
	if (key == "left_trigger" || key == "lt" || key == "trigger_l") {
		return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
	}
	if (key == "right_trigger" || key == "rt" || key == "trigger_r") {
		return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
	}

	return std::nullopt;
}

inline auto ControllerStickFromString(std::string& str) -> std::optional<int> {
	std::string key = normalize_name(str);
	if (key.rfind("controller/stick/", 0) != 0) {
		return std::nullopt;
	}
	key = key.substr(17);

	if (key == "left" || key == "l" || key == "left_stick" || key == "left_thumb") {
		return 0;
	}
	if (key == "right" || key == "r" || key == "right_stick" || key == "right_thumb") {
		return 1;
	}

	return std::nullopt;
}

}
