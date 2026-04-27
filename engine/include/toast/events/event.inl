#pragma once
#include "event.hpp"

#include <mutex>
#include <print>
#include <type_traits>

namespace event {

template<typename T>
Event<T>::G::G() noexcept {
	static_assert(std::is_base_of_v<Event<T>, T>, "CONTRACT VIOLATION: You Must Inhert as 'struct Derived : Event<Derived>'");
	std::println("Registering {} as Event", typeid(T).name());

	// add function to the Event vtable
	_detail::unsubscribe_map.emplace(typeid(T), [](std::any iter) {
		auto it = std::any_cast<iterator_t>(iter);
		unsubscribe(it);
	});
}

template<typename T>
auto Event<T>::subscribe(char priority, callback_t&& callback) noexcept -> iterator_t {
	std::println("Subscribing Callback To: {}", typeid(T).name());
	std::scoped_lock _(g.mutex);
	g.cached = false;
	return g.callbacks.emplace(priority, callback);
}

template<typename T>
void Event<T>::unsubscribe(iterator_t it) noexcept {
	std::println("Unsubscribing Callback To: {}", typeid(T).name());
	std::scoped_lock _(g.mutex);
	g.cached = false;
	g.callbacks.erase(it);
}

template<typename T>
void Event<T>::notify() noexcept {
	std::println("Notifying Event: {}", typeid(*this).name());
	{
		// build cached list of all the callbacks in order
		// locks mutex for the shortest period possible
		// improves cache locality
		std::scoped_lock _(g.mutex);
		if (not g.cached) {
			g.cached = true;
			g.processing.clear();
			for (auto& [priority, callback] : g.callbacks) {
				g.processing.emplace_back(callback);
			}
		}
	}

	// disptaches all of the callbacks
	for (auto& callback : g.processing) {
		bool handled = callback(static_cast<T&>(*this));
		if (handled) {
			return;
		}
	}
}

template<typename T, typename... Args>
  requires std::is_base_of_v<_detail::IEvent, T>
void send(Args&&... args) noexcept {
	static_assert(std::is_constructible_v<T, Args...>, "Invalid Construtor For Type T");
	std::println("Sending Event: {}", typeid(T).name());
	// Allocate and enqueue event
	std::scoped_lock _(_detail::mutex);
	void* memory = _detail::allocate(sizeof(T), alignof(T));
	assert(memory);
	_detail::IEvent* event = new (memory) T(std::forward<Args>(args)...);
#ifdef DEBUG
	event->stacktrace = std::stacktrace::current(1);
#endif
}

}
