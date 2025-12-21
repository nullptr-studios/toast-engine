/// @file KeyCodes.hpp
/// @date 13 Dec 2025
/// @author Xein
/// This is probably the ugliest file you'll ever see but here we go
/// Enjoy this madness

#pragma once

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <optional>

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

// Sentinel for mouse special codes
constexpr unsigned MOUSE_POSITION_CODE = -1;
constexpr unsigned MOUSE_SCROLL_X_CODE = -2;
constexpr unsigned MOUSE_SCROLL_Y_CODE = -3;

// Helpers: trim and lowercase/normalize
static void trim_inplace(std::string& s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
		        return !std::isspace(ch);
	        }));
	s.erase(
	    std::find_if(
	        s.rbegin(),
	        s.rend(),
	        [](unsigned char ch) {
		        return !std::isspace(ch);
	        }
	    ).base(),
	    s.end()
	);
}

static std::string to_lower_copy(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
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

// Forward declarations
inline auto KeyboardKeycodeFromString(std::string& str) -> std::optional<int>;
inline auto MouseKeycodeFromString(std::string& str) -> std::optional<int>;
// inline auto MouseScrollFromString(std::string& str) -> std::optional<int>;
// inline auto MouseAxisFromString(std::string& str) -> std::optional<int>;
inline auto ControllerButtonFromString(std::string& str) -> std::optional<int>;
inline auto ControllerAxisFromString(std::string& str) -> std::optional<int>;
inline auto ControllerStickFromString(std::string& str) -> std::optional<int>;

// Top-level dispatcher
inline auto KeycodeFromString(std::string& str) -> std::optional<std::pair<unsigned, Device>> {
	// To lowercase
	std::ranges::transform(str, str.begin(), [](unsigned char c) {
		return std::tolower(c);
	});

	// Treat explicit controller/mouse prefixes first
	if (str.rfind("mouse/", 0) == 0) {
		auto result = MouseKeycodeFromString(str);
		if (!result.has_value()) {
			return std::nullopt;
		}

		int code = result.value();
		if (code == -1) {    // position sentinel from mouse helper
			return std::make_pair(MOUSE_POSITION_CODE, Device::Mouse);
		}
		return std::make_pair(static_cast<unsigned>(code), Device::Mouse);
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

	// try keyboard parsing for anything else
	auto result = KeyboardKeycodeFromString(str);
	if (!result.has_value()) {
		return std::nullopt;
	}
	return std::make_pair(static_cast<unsigned>(result.value()), Device::Keyboard);
}

// Keyboard parsing
inline auto KeyboardKeycodeFromString(std::string& str) -> std::optional<int> {
	// trim
	auto ltrim = [](std::string& s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			        return !std::isspace(ch);
		        }));
	};
	auto rtrim = [](std::string& s) {
		s.erase(
		    std::find_if(
		        s.rbegin(),
		        s.rend(),
		        [](unsigned char ch) {
			        return !std::isspace(ch);
		        }
		    ).base(),
		    s.end()
		);
	};
	ltrim(str);
	rtrim(str);
	if (str.empty()) {
		return std::nullopt;
	}

	// Helper to lowercase
	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
			return std::tolower(c);
		});
		return s;
	};

	// Check for numpad prefix
	std::string low = to_lower(str);
	const std::string numpad_prefix = "numpad/";
	if (low.rfind(numpad_prefix, 0) == 0) {    // starts with "numpad/"
		std::string key = low.substr(numpad_prefix.size());
		// normalize common variants
		for (auto& c : key) {
			if (c == ' ' || c == '-') {
				c = '_';
			}
		}

		// digits
		if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
			int digit = key[0] - '0';
			switch (digit) {
				case 0: return GLFW_KEY_KP_0;
				case 1: return GLFW_KEY_KP_1;
				case 2: return GLFW_KEY_KP_2;
				case 3: return GLFW_KEY_KP_3;
				case 4: return GLFW_KEY_KP_4;
				case 5: return GLFW_KEY_KP_5;
				case 6: return GLFW_KEY_KP_6;
				case 7: return GLFW_KEY_KP_7;
				case 8: return GLFW_KEY_KP_8;
				case 9: return GLFW_KEY_KP_9;
			}
		}

		if (key == "decimal" || key == "." || key == "dot") {
			return GLFW_KEY_KP_DECIMAL;
		}
		if (key == "enter" || key == "return") {
			return GLFW_KEY_KP_ENTER;
		}
		if (key == "add" || key == "+" || key == "plus") {
			return GLFW_KEY_KP_ADD;
		}
		if (key == "subtract" || key == "sub" || key == "-" || key == "minus") {
			return GLFW_KEY_KP_SUBTRACT;
		}
		if (key == "multiply" || key == "mul" || key == "*" || key == "asterisk") {
			return GLFW_KEY_KP_MULTIPLY;
		}
		if (key == "divide" || key == "div" || key == "/") {
			return GLFW_KEY_KP_DIVIDE;
		}
		if (key == "equal" || key == "=") {
			return GLFW_KEY_KP_EQUAL;
		}

		return std::nullopt;
	}

	// Single-character shortcuts
	if (str.size() == 1) {
		unsigned char c = static_cast<unsigned char>(str[0]);
		if (std::isalpha(c)) {
			char lowc = static_cast<char>(std::tolower(c));
			return GLFW_KEY_A + (lowc - 'a');
		}
		if (std::isdigit(c)) {
			return GLFW_KEY_0 + (c - '0');
		}

		switch (c) {
			case ' ': return GLFW_KEY_SPACE;
			case '[': return GLFW_KEY_LEFT_BRACKET;
			case ']': return GLFW_KEY_RIGHT_BRACKET;
			case '\\': return GLFW_KEY_BACKSLASH;
			case ';': return GLFW_KEY_SEMICOLON;
			case '\'': return GLFW_KEY_APOSTROPHE;
			case ',': return GLFW_KEY_COMMA;
			case '.': return GLFW_KEY_PERIOD;
			case '/': return GLFW_KEY_SLASH;
			case '-': return GLFW_KEY_MINUS;
			case '=': return GLFW_KEY_EQUAL;
			case '`': return GLFW_KEY_GRAVE_ACCENT;
			case '<': return GLFW_KEY_COMMA;           // '<' is shift+','
			case '>': return GLFW_KEY_PERIOD;          // '>' is shift+'.'
			case ':': return GLFW_KEY_SEMICOLON;       // ':' is shift+';'
			case '"': return GLFW_KEY_APOSTROPHE;      // '"' is shift+'\''
			case '|': return GLFW_KEY_BACKSLASH;       // '|' is shift+'\'
			case '?': return GLFW_KEY_SLASH;           // '?' is shift+'/'
			case '+': return GLFW_KEY_EQUAL;           // '+' is shift+'=' -> map to EQUAL
			case '_': return GLFW_KEY_MINUS;           // '_' shift + '-'
			case '~': return GLFW_KEY_GRAVE_ACCENT;    // '~' shift+``
			case '!': return GLFW_KEY_1;               // '!' shift + '1'
			case '@': return GLFW_KEY_2;
			case '#': return GLFW_KEY_3;
			case '$': return GLFW_KEY_4;
			case '%': return GLFW_KEY_5;
			case '^': return GLFW_KEY_6;
			case '&': return GLFW_KEY_7;
			case '*': return GLFW_KEY_8;
			case '(': return GLFW_KEY_9;
			case ')': return GLFW_KEY_0;
			default: break;
		}
	}

	// Normalize textual input
	std::string n = to_lower(str);
	for (auto& c : n) {
		if (c == ' ' || c == '-') {
			c = '_';
		}
	}

	// Function keys F1..F25
	if (n.size() >= 2 && n[0] == 'f') {
		int idx = 0;
		try {
			idx = std::stoi(n.substr(1));
		} catch (...) { idx = 0; }
		if (idx >= 1 && idx <= 25) {
			return GLFW_KEY_F1 + (idx - 1);
		}
	}

	// Named mapping
	static const std::unordered_map<std::string, int> map {
		// common keys
		{		     "space",         GLFW_KEY_SPACE },
		{		     "enter",         GLFW_KEY_ENTER },
		{		    "return",         GLFW_KEY_ENTER },
		{		       "tab",           GLFW_KEY_TAB },
		{     "backspace",     GLFW_KEY_BACKSPACE },
		{		    "escape",        GLFW_KEY_ESCAPE },
		{		       "esc",        GLFW_KEY_ESCAPE },
		{		    "insert",        GLFW_KEY_INSERT },
		{		    "delete",        GLFW_KEY_DELETE },
		{		      "home",          GLFW_KEY_HOME },
		{		       "end",           GLFW_KEY_END },
		{		   "page_up",       GLFW_KEY_PAGE_UP },
		{     "page_down",     GLFW_KEY_PAGE_DOWN },
		{		        "up",		        GLFW_KEY_UP },
		{		      "down",          GLFW_KEY_DOWN },
		{		      "left",          GLFW_KEY_LEFT },
		{		     "right",         GLFW_KEY_RIGHT },

		// modifiers
		{    "left_shift",    GLFW_KEY_LEFT_SHIFT },
		{   "right_shift",   GLFW_KEY_RIGHT_SHIFT },
		{  "left_control",  GLFW_KEY_LEFT_CONTROL },
		{ "right_control", GLFW_KEY_RIGHT_CONTROL },
		{     "left_ctrl",  GLFW_KEY_LEFT_CONTROL },
		{    "right_ctrl", GLFW_KEY_RIGHT_CONTROL },
		{		  "left_alt",      GLFW_KEY_LEFT_ALT },
		{     "right_alt",     GLFW_KEY_RIGHT_ALT },
		{    "left_super",    GLFW_KEY_LEFT_SUPER },
		{   "right_super",   GLFW_KEY_RIGHT_SUPER },
		{     "caps_lock",     GLFW_KEY_CAPS_LOCK },
		{   "scroll_lock",   GLFW_KEY_SCROLL_LOCK },
		{		  "num_lock",      GLFW_KEY_NUM_LOCK },
		{		      "menu",          GLFW_KEY_MENU },

		// punctuation by name
		{  "left_bracket",  GLFW_KEY_LEFT_BRACKET },
		{ "right_bracket", GLFW_KEY_RIGHT_BRACKET },
		{     "backslash",     GLFW_KEY_BACKSLASH },
		{     "semicolon",     GLFW_KEY_SEMICOLON },
		{    "apostrophe",    GLFW_KEY_APOSTROPHE },
		{		     "comma",         GLFW_KEY_COMMA },
		{		    "period",        GLFW_KEY_PERIOD },
		{		     "slash",         GLFW_KEY_SLASH },
		{		     "minus",         GLFW_KEY_MINUS },
		{		     "equal",         GLFW_KEY_EQUAL },
		{  "grave_accent",  GLFW_KEY_GRAVE_ACCENT },

		// keypad synonyms
		{		      "kp_0",          GLFW_KEY_KP_0 },
		{		      "kp_1",          GLFW_KEY_KP_1 },
		{		      "kp_2",          GLFW_KEY_KP_2 },
		{		      "kp_3",          GLFW_KEY_KP_3 },
		{		      "kp_4",          GLFW_KEY_KP_4 },
		{		      "kp_5",          GLFW_KEY_KP_5 },
		{		      "kp_6",          GLFW_KEY_KP_6 },
		{		      "kp_7",          GLFW_KEY_KP_7 },
		{		      "kp_8",          GLFW_KEY_KP_8 },
		{		      "kp_9",          GLFW_KEY_KP_9 },
		{    "kp_decimal",    GLFW_KEY_KP_DECIMAL },
		{		  "kp_enter",      GLFW_KEY_KP_ENTER },
		{		    "kp_add",        GLFW_KEY_KP_ADD },
		{   "kp_subtract",   GLFW_KEY_KP_SUBTRACT },
		{   "kp_multiply",   GLFW_KEY_KP_MULTIPLY },
		{     "kp_divide",     GLFW_KEY_KP_DIVIDE },
		{		  "kp_equal",      GLFW_KEY_KP_EQUAL },

		// system keys
		{  "print_screen",  GLFW_KEY_PRINT_SCREEN },
		{		     "pause",         GLFW_KEY_PAUSE },
		{   "pause_break",         GLFW_KEY_PAUSE },
	};

	auto it = map.find(n);
	if (it != map.end()) {
		return it->second;
	}

	return std::nullopt;
}

// Mouse parsing. str expected to start with "mouse/"
inline auto MouseKeycodeFromString(std::string& str) -> std::optional<int> {
	std::string s = normalize_name(str);
	const std::string prefix = "mouse/";
	if (s.rfind(prefix, 0) != 0) {
		return std::nullopt;
	}
	std::string key = s.substr(prefix.size());
	if (key.empty()) {
		return std::nullopt;
	}

	// mouse/position -> sentinel -1
	// mouse/x -> sentinel -2
	// mouse/y -> sentinel -3
	if (key == "position" || key == "pos") {
		return MOUSE_POSITION_CODE;
	}
	if (key == "scrollx" || key == "x") {
		return MOUSE_SCROLL_X_CODE;
	}
	if (key == "scrolly" || key == "scroll" || key == "y") {
		return MOUSE_SCROLL_Y_CODE;
	}

	// common names
	if (key == "left") {
		return GLFW_MOUSE_BUTTON_LEFT;
	}
	if (key == "right") {
		return GLFW_MOUSE_BUTTON_RIGHT;
	}
	if (key == "middle" || key == "wheel") {
		return GLFW_MOUSE_BUTTON_MIDDLE;
	}

	// "buttonN" or just numeric "0","1", etc.
	if (key.rfind("button", 0) == 0) {
		std::string num = key.substr(6);    // after "button"
		if (!num.empty() && std::all_of(num.begin(), num.end(), ::isdigit)) {
			int idx = std::atoi(num.c_str());
			if (idx >= 0 && idx <= GLFW_MOUSE_BUTTON_LAST) {
				return idx;
			}
		}
		return std::nullopt;
	}

	// numeric raw
	if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit)) {
		int idx = std::atoi(key.c_str());
		if (idx >= 0 && idx <= GLFW_MOUSE_BUTTON_LAST) {
			return idx;
		}
		return std::nullopt;
	}

	// synonyms
	if (key == "btn_left" || key == "l") {
		return GLFW_MOUSE_BUTTON_LEFT;
	}
	if (key == "btn_right" || key == "r") {
		return GLFW_MOUSE_BUTTON_RIGHT;
	}
	if (key == "btn_middle" || key == "m") {
		return GLFW_MOUSE_BUTTON_MIDDLE;
	}

	return std::nullopt;
}

// Controller button parsing. str expected to start with "controller/button/"
inline auto ControllerButtonFromString(std::string& str) -> std::optional<int> {
	std::string s = normalize_name(str);
	const std::string prefix = "controller/button/";
	if (s.rfind(prefix, 0) != 0) {
		return std::nullopt;
	}
	std::string key = s.substr(prefix.size());
	if (key.empty()) {
		return std::nullopt;
	}

	// numeric button e.g., controller/button/0
	if (std::all_of(key.begin(), key.end(), ::isdigit)) {
		int idx = std::atoi(key.c_str());
		if (idx >= 0 && idx <= GLFW_GAMEPAD_BUTTON_LAST) {
			return idx;
		}
		return std::nullopt;
	}

	// common controller button names / synonyms
	if (key == "a" || key == "cross") {
		return GLFW_GAMEPAD_BUTTON_A;
	}
	if (key == "b" || key == "circle") {
		return GLFW_GAMEPAD_BUTTON_B;
	}
	if (key == "x" || key == "square") {
		return GLFW_GAMEPAD_BUTTON_X;
	}
	if (key == "y" || key == "triangle") {
		return GLFW_GAMEPAD_BUTTON_Y;
	}

	if (key == "left_bumper" || key == "l_bumper" || key == "lb" || key == "left_shoulder") {
		return GLFW_GAMEPAD_BUTTON_LEFT_BUMPER;
	}
	if (key == "right_bumper" || key == "r_bumper" || key == "rb" || key == "right_shoulder") {
		return GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER;
	}

	if (key == "back" || key == "select") {
		return GLFW_GAMEPAD_BUTTON_BACK;
	}
	if (key == "guide" || key == "home") {
		return GLFW_GAMEPAD_BUTTON_GUIDE;
	}
	if (key == "start") {
		return GLFW_GAMEPAD_BUTTON_START;
	}

	if (key == "left_thumb" || key == "left_stick" || key == "l_thumb" || key == "ls") {
		return GLFW_GAMEPAD_BUTTON_LEFT_THUMB;
	}
	if (key == "right_thumb" || key == "right_stick" || key == "r_thumb" || key == "rs") {
		return GLFW_GAMEPAD_BUTTON_RIGHT_THUMB;
	}

	if (key == "dpad_up" || key == "dp_up" || key == "up") {
		return GLFW_GAMEPAD_BUTTON_DPAD_UP;
	}
	if (key == "dpad_right" || key == "dp_right" || key == "right") {
		return GLFW_GAMEPAD_BUTTON_DPAD_RIGHT;
	}
	if (key == "dpad_down" || key == "dp_down" || key == "down") {
		return GLFW_GAMEPAD_BUTTON_DPAD_DOWN;
	}
	if (key == "dpad_left" || key == "dp_left" || key == "left") {
		return GLFW_GAMEPAD_BUTTON_DPAD_LEFT;
	}

	return std::nullopt;
}

// Controller axis parsing. str expected to start with "controller/axis/"
inline auto ControllerAxisFromString(std::string& str) -> std::optional<int> {
	std::string s = normalize_name(str);
	const std::string prefix = "controller/axis/";
	if (s.rfind(prefix, 0) != 0) {
		return std::nullopt;
	}
	std::string key = s.substr(prefix.size());
	if (key.empty()) {
		return std::nullopt;
	}

	// numeric axis
	if (std::all_of(key.begin(), key.end(), ::isdigit)) {
		int idx = std::atoi(key.c_str());
		if (idx >= 0 && idx <= GLFW_GAMEPAD_AXIS_LAST) {
			return idx;
		}
		return std::nullopt;
	}

	// common mappings
	if (key == "left_x" || key == "lx" || key == "leftstick_x" || key == "left_stick_x") {
		return GLFW_GAMEPAD_AXIS_LEFT_X;
	}
	if (key == "left_y" || key == "ly" || key == "leftstick_y" || key == "left_stick_y") {
		return GLFW_GAMEPAD_AXIS_LEFT_Y;
	}
	if (key == "right_x" || key == "rx" || key == "rightstick_x" || key == "right_stick_x") {
		return GLFW_GAMEPAD_AXIS_RIGHT_X;
	}
	if (key == "right_y" || key == "ry" || key == "rightstick_y" || key == "right_stick_y") {
		return GLFW_GAMEPAD_AXIS_RIGHT_Y;
	}
	if (key == "left_trigger" || key == "l_trigger" || key == "l_trig" || key == "lt") {
		return GLFW_GAMEPAD_AXIS_LEFT_TRIGGER;
	}
	if (key == "right_trigger" || key == "r_trigger" || key == "r_trig" || key == "rt") {
		return GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER;
	}

	// alternative trigger names
	if (key == "trigger_l" || key == "trigger_left") {
		return GLFW_GAMEPAD_AXIS_LEFT_TRIGGER;
	}
	if (key == "trigger_r" || key == "trigger_right") {
		return GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER;
	}

	return std::nullopt;
}

// Controller stick parsing. str expected to start with "controller/stick/"
// returns 0 for left, 1 for right
inline auto ControllerStickFromString(std::string& str) -> std::optional<int> {
	std::string s = normalize_name(str);
	const std::string prefix = "controller/stick/";
	if (s.rfind(prefix, 0) != 0) {
		return std::nullopt;
	}
	std::string key = s.substr(prefix.size());
	if (key.empty()) {
		return std::nullopt;
	}

	if (key == "left" || key == "l" || key == "left_stick" || key == "left_thumb") {
		return 0;
	}
	if (key == "right" || key == "r" || key == "right_stick" || key == "right_thumb") {
		return 1;
	}

	return std::nullopt;
}

}
