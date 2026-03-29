#include "Toast/Window/WindowEvents.hpp"

#include "Toast/Event/ListenerComponent.hpp"

namespace event {

void WindowKey::Callback(int key, int scancode, int action, int mods) {
	Send(new WindowKey(key, scancode, action, mods));
}

void WindowChar::Callback(unsigned key) {
	Send(new WindowChar(key));
}

void WindowMousePosition::Callback(double x_pos, double y_pos) {
	Send(new WindowMousePosition(x_pos, y_pos));
}

void WindowMouseButton::Callback(int button, int action, int mods) {
	Send(new WindowMouseButton(button, action, mods));
}

void WindowMouseScroll::Callback(double x_offset, double y_offset) {
	Send(new WindowMouseScroll(x_offset, y_offset));
}

void WindowInputDevice::Callback(int jid, int event) {
	Send(new WindowInputDevice(jid, event));
}

void WindowDrop::Callback(std::vector<std::string> files) {
	Send(new WindowDrop(std::move(files)));
}

void WindowResize::Callback(int width, int height) {
	Send(new WindowResize(width, height));
}

}
