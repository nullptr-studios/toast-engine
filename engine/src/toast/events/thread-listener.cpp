#include "thread-listener.hpp"

#include "event.hpp"

#include <mutex>
#include <ranges>

namespace event {

ThreadListener::ThreadListener() {
	m.enabled = new std::atomic<bool>(true);
}

ThreadListener::ThreadListener(bool state) {
	m.enabled = new std::atomic<bool>(state);
}

ThreadListener::~ThreadListener() {
	m.enabled->store(false);
	for (auto& [type, iterator] : m.recievers) {
		EventSystem::unsubscribe_map[type](iterator);
	}
	{
		std::scoped_lock lock(m.queue_mutex);
		for (auto* event : m.event_queue) {
			delete event;
		}
	}
	{
		std::scoped_lock _(EventSystem::deletion_mutex);
		auto deleter = [](void* p) { delete static_cast<std::atomic<bool>*>(p); };
		EventSystem::deletion_queue.emplace_back(m.enabled, deleter);
	}
}

void ThreadListener::clear() {
	bool state = m.enabled->exchange(false);
	for (auto& [type, iterator] : m.recievers) {
		EventSystem::unsubscribe_map[type](iterator);
	}
	m.recievers.clear();
	m.callbacks.clear();
	{
		std::scoped_lock lock(m.queue_mutex);
		for (auto* event : m.event_queue) {
			delete event;
		}
		m.event_queue.clear();
	}
	m.enabled->store(state);
}

void ThreadListener::pollEvents() {
	std::vector<_detail::IEvent*> temp_queue;
	{
		std::scoped_lock lock(m.queue_mutex);
		temp_queue = std::move(m.event_queue);
		m.event_queue.clear();
	}

	for (auto* event : temp_queue) {
		for (auto& cb_handle : std::views::values(m.callbacks)) {
			if (cb_handle.type == typeid(*event)) {
				if (cb_handle.callback(*event)) {
					break;
				}
			}
		}
		delete event;
	}
}

void ThreadListener::enabled(bool state) {    // NOLINT(readability-make-member-function-const)

	m.enabled->store(state);
}

[[nodiscard]]
auto ThreadListener::enabled() const -> bool {
	return m.enabled->load();
}

}
