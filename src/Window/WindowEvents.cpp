#include "GLFW/glfw3.h"

#include <Engine/Event/EventSystem.hpp>
#include <Engine/Window/WindowEvents.hpp>

namespace event {

void WindowKey::Callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	Send(new WindowKey(key, scancode, action, mods));
}

void WindowChar::Callback(GLFWwindow* window, unsigned key) {
	Send(new WindowChar(key));
}

void WindowMousePosition::Callback(GLFWwindow* window, double x_pos, double y_pos) {
	Send(new WindowMousePosition(x_pos, y_pos));
}

void WindowMouseButton::Callback(GLFWwindow* window, int button, int action, int mods) {
	Send(new WindowMouseButton(button, action, mods));
}

void WindowMouseScroll::Callback(GLFWwindow* window, double x_offset, double y_offset) {
	Send(new WindowMouseScroll(x_offset, y_offset));
}

void WindowInputDevice::Callback(int jid, int event) {
	Send(new WindowInputDevice(jid, event));
}

void WindowDrop::Callback(GLFWwindow* window, int count, const char** paths) {
	Send(new WindowDrop(count, paths));
}

void WindowResize::Callback(GLFWwindow* window, int width, int height) {
	Send(new WindowResize(width, height));
}

}
