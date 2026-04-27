#include "listener.hpp"

#include "event.hpp"

namespace event {

Listener::Listener() {
	m.enabled = true;
}

Listener::~Listener() {
	for (auto& [type, name, callback] : m.callbacks) {
		_detail::unsubscribe_map[type](callback);
	}
}

}
