#pragma once
#include "event.hpp"
#include "toast/log.hpp"

#include <mutex>
#include <type_traits>

namespace event {

template<typename T>
Event<T>::G::G() noexcept {
	static_assert(std::is_base_of_v<Event<T>, T>, "CONTRACT VIOLATION: You Must Inhert as 'struct Derived : Event<Derived>'");
	TOAST_INFO(_detail::IEvent,"Registering Event Type: {}",typeid(T).name());
	// add function to the Event vtable
	_detail::unsubscribe_map.emplace(typeid(T), [](std::any iter) {
		auto it = std::any_cast<iterator_t>(iter);
		unsubscribe(it);
	});
}

template<typename T>
auto Event<T>::subscribe(char priority, callback_t&& callback) noexcept -> iterator_t {
	TOAST_TRACE(_detail::IEvent, "Subscribing Callback To: {}", typeid(T).name());

	auto cb = new callback_t(std::move(callback));
	{
		std::scoped_lock _(g.mutex);
		g.cached = false;
		return g.callbacks.emplace(priority, cb);
	}
}

template<typename T>
void Event<T>::unsubscribe(iterator_t it) noexcept {
	TOAST_TRACE(_detail::IEvent, "Unsubscribing Callback To: {}", typeid(T).name());

	{
		std::scoped_lock _(g.mutex);
		auto deleter = [](void* p) { delete static_cast<std::move_only_function<bool(T&)>*>(p); };
		_detail::deletion_queue.emplace_back((*it).second, deleter);
		g.cached = false;
		g.callbacks.erase(it);
	}
}

template<typename T>
void Event<T>::notify() noexcept {
	TOAST_TRACE(_detail::IEvent, "Notifying Event: {}", typeid(T).name());
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
		bool handled = (*callback)(static_cast<T&>(*this));
		if (handled) {
			return;
		}
	}
}

template<typename T, typename... Args>
  requires std::is_base_of_v<_detail::IEvent, T>
void send(Args&&... args) noexcept {
	static_assert(std::is_constructible_v<T, Args...>, "Invalid Construtor For Type T");
	TOAST_TRACE(_detail::IEvent, "Sending Event: {}", typeid(T).name());
	// Allocate and enqueue event
	{
		std::scoped_lock _(_detail::mutex);
		void* memory = _detail::allocate(sizeof(T), alignof(T));
		assert(memory);
		_detail::IEvent* event = new (memory) T(std::forward<Args>(args)...);
#ifdef DEBUG
		event->stacktrace = std::stacktrace::current(1);
#endif
	}
}

}
