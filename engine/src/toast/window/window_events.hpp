/***
 *  @file window_events.hpp
 *  @author Xein
 *  @date 06 May 25
 *  @brief All the different events a window may have
 */

#pragma once
#include "toast/events/event.hpp"

namespace event {

constexpr int window_input_released = 0;
constexpr int window_input_pressed = 1;
constexpr int window_input_repeated = 2;

constexpr int window_input_device_disconnected = 0;
constexpr int window_input_device_connected = 1;

/// @brief Send this event to tell the engine to close the window
struct ExitApplication : Event<ExitApplication> { };

/// @brief Send this event to tell the engine to close the window
struct WindowClose : Event<WindowClose> {
	uint32_t window_id;

	explicit WindowClose(uint32_t window_id) : window_id(window_id) { }
};

/// @brief Event sent when user drag and drops files to the window
struct WindowDrop : Event<WindowDrop> {
	/// @brief Path to the files thrown to the window
	std::vector<std::string> files;

	explicit WindowDrop(std::vector<std::string>&& input_files) : files(std::move(input_files)) { }
};

/// @brief Event sent when user presses a key
struct WindowKey : Event<WindowKey> {
	int key;         ///< @brief SDL key code
	int scancode;    ///< @brief SDL scancode
	int action;      ///< @brief 0 released, 1 pressed, 2 repeated
	int mods;        ///< @brief Modifier flags

	WindowKey(int key, int scancode, int action, int mods) : key(key), scancode(scancode), action(action), mods(mods) { }
};

/// @brief Event sent when user presses a character
struct WindowChar : Event<WindowChar> {
	unsigned key;

	WindowChar(unsigned key) : key(key) { }
};

/// @brief Event sent when user moves the mouse
struct WindowMousePosition : Event<WindowMousePosition> {
	float x, y;

	WindowMousePosition(float x_pos, float y_pos) : x(x_pos), y(y_pos) { }
};

/// @brief Event sent when user clicks a button on the mouse
struct WindowMouseButton : Event<WindowMouseButton> {
	int button, action, mods;

	WindowMouseButton(int button, int action, int mods) : button(button), action(action), mods(mods) { }
};

/// @brief Event sent when user uses the scroll wheel
struct WindowMouseScroll : Event<WindowMouseScroll> {
	float x, y;

	WindowMouseScroll(float x_offset, float y_offset) : x(x_offset), y(y_offset) { }
};

/// @brief Event sent when the window framebuffer is resized
///
/// Use this on the renderer to get when the window is resized, something along these lines:
/// @code
/// m_listener.Subscribe<WindowResize>([](WindowResize* e)->bool {
///   glViewport(0, 0, e->width, e->height);
/// });
/// @endcode
struct WindowResize : Event<WindowResize> {
	int width, height;

	WindowResize(int width, int height) : width(width), height(height) { }
};

}
