/// @file WindowEvents.hpp
/// @author Xein
/// @date 06/05/25
/// @brief Stores all the different events a function has

#pragma once
#include "Toast/Event/Event.hpp"
#include "Toast/Input/InputListener.hpp"
#include "Window.hpp"

namespace event {

constexpr int WINDOW_INPUT_RELEASED = 0;
constexpr int WINDOW_INPUT_PRESSED = 1;
constexpr int WINDOW_INPUT_REPEATED = 2;

constexpr int WINDOW_INPUT_DEVICE_DISCONNECTED = 0;
constexpr int WINDOW_INPUT_DEVICE_CONNECTED = 1;

/// @brief Send this event to tell the engine to close the window
struct WindowClose : Event<WindowClose> { };

/// @brief Event sent when user drag and drops files to the window
struct WindowDrop : Event<WindowDrop> {
	/// @brief Path to the files thrown to the window
	std::vector<std::string> files;

	static void Callback(std::vector<std::string> files);

	explicit WindowDrop(std::vector<std::string> input_files) : files(std::move(input_files)) { }
};

/// @brief Event sent when user presses a key
struct WindowKey : Event<WindowKey> {
	int key;         ///< @brief SDL key code
	int scancode;    ///< @brief SDL scancode
	int action;      ///< @brief 0 released, 1 pressed, 2 repeated
	int mods;        ///< @brief Modifier flags

	static void Callback(int key, int scancode, int action, int mods);

	WindowKey(int key, int scancode, int action, int mods) : key(key), scancode(scancode), action(action), mods(mods) { }
};

/// @brief Event sent when user presses a character
struct WindowChar : Event<WindowChar> {
	unsigned key;

	static void Callback(unsigned key);

	WindowChar(unsigned key) : key(key) { }
};

/// @brief Event sent when user moves the mouse
struct WindowMousePosition : Event<WindowMousePosition> {
	double x, y;

	static void Callback(double x_pos, double y_pos);

	WindowMousePosition(double x_pos, double y_pos) : x(x_pos), y(y_pos) { }
};

/// @brief Event sent when user clicks a button on the mouse
struct WindowMouseButton : Event<WindowMouseButton> {
	int button, action, mods;

	static void Callback(int button, int action, int mods);

	WindowMouseButton(int button, int action, int mods) : button(button), action(action), mods(mods) { }
};

/// @brief Event sent when user uses the scroll wheel
struct WindowMouseScroll : Event<WindowMouseScroll> {
	double x, y;

	static void Callback(double x_offset, double y_offset);

	WindowMouseScroll(double x_offset, double y_offset) : x(x_offset), y(y_offset) { }
};

/// @brief Event sent when user connects or disconnects a controller
struct WindowInputDevice : Event<WindowInputDevice> {
	int jid;      ///< @brief Assigned ID of the gamepad
	int event;    ///< @brief Device event code

	static void Callback(int jid, int event);

	WindowInputDevice(int jid, int event) : jid(jid), event(event) { }
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

	static void Callback(int width, int height);

	WindowResize(int width, int height) : width(width), height(height) {
		input::SetViewportPosition({ 0, 0 });
		input::SetViewportSize(toast::Window::GetInstance()->GetFramebufferSize());
	}
};

}
