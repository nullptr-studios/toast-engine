#include "Toast/Input/InputListener.hpp"

#include "InputSystem.hpp"

namespace input {

void SetLayout(std::string_view name) {
	InputSystem::ActiveLayout(name);
}

void SetState(std::string_view state) {
	InputSystem::SetState(state);
}

Listener::Listener() {
	InputSystem::RegisterListener(this);
}

Listener::~Listener() {
	InputSystem::UnregisterListener(this);
}

void Listener::Subscribe0D(const std::string& name, Callback0D&& callback) {
	m.callbacks0d.insert({ name, std::move(callback) });
	TOAST_TRACE("Subscribing action, size: {0}", m.callbacks0d.size());
}

void Listener::Subscribe1D(const std::string& name, Callback1D&& callback) {
	m.callbacks1d.insert({ name, std::move(callback) });
	TOAST_TRACE("Subscribing action, size: {0}", m.callbacks1d.size());
}

void Listener::Subscribe2D(const std::string& name, Callback2D&& callback) {
	m.callbacks2d.insert({ name, std::move(callback) });
	TOAST_TRACE("Subscribing action, size: {0}", m.callbacks2d.size());
}

void Listener::Unsubscribe0D(const std::string& name) {
	m.callbacks0d.erase(name);
}

void Listener::Unsubscribe1D(const std::string& name) {
	m.callbacks1d.erase(name);
}

void Listener::Unsubscribe2D(const std::string& name) {
	m.callbacks2d.erase(name);
}

}
