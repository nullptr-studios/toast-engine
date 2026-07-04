#include "listener.hpp"

#include "event.hpp"

namespace event {

Listener::Listener() {
	m.enabled = new std::atomic<bool>(true);
}

Listener::Listener(bool state) {
	m.enabled = new std::atomic<bool>(state);
}

Listener::~Listener() {
	clear();
}

void Listener::clear() noexcept {
	for (auto& [type, name, callback] : m.callbacks) {
		EventSystem::unsubscribe_map[type](callback);
	}

	{
		std::scoped_lock _(EventSystem::deletion_mutex);
		auto deleter = [](void* p) { delete static_cast<std::atomic<bool>*>(p); };
		EventSystem::deletion_queue.emplace_back(m.enabled, deleter);
	}
}

void Listener::enabled(bool state) {    // NOLINT(readability-make-member-function-const)
	m.enabled->store(state);
}

[[nodiscard]]
auto Listener::enabled() const -> bool {
	return m.enabled->load();
}

}
