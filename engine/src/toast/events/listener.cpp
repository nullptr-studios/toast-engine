#include "listener.hpp"

#include "event.hpp"
#include "toast/events/event_system.hpp"

namespace event {

Listener::Listener() {
	m.enabled = true;
}

Listener::~Listener() {
	for (auto& [type, name, callback] : m.callbacks) {
		EventSystem::unsubscribe_map[type](callback);
	}
}

void Listener::enabled(bool state) {
	m.enabled = state;
}

[[nodiscard]]
auto Listener::enabled() const -> bool {
	return m.enabled;
}
}
