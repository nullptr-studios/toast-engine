#pragma once
#include "event.hpp"
#include "toast/log.hpp"

#include <mutex>
#include <tracy/Tracy.hpp>
#include <type_traits>

namespace event {

template<typename T>
void Event<T>::ensureRegistered() noexcept {
	static const bool registered = [] {
		EventSystem::registerEvent<T>();
		return true;
	}();
	(void)registered;
}

template<typename T>
auto Event<T>::subscribe(char priority, callback_t&& callback) noexcept -> iterator_t {
	ensureRegistered();

	auto cb = new callback_t(std::move(callback));
	auto& g = EventSystem::event_data[typeid(T)];
	{
		std::scoped_lock _(g.mutex);
		g.cached = false;
		return g.callbacks.emplace(priority, static_cast<void*>(cb));
	}
}

template<typename T>
void Event<T>::unsubscribe(iterator_t it) noexcept {
	ensureRegistered();

	auto& g = EventSystem::event_data[typeid(T)];
	{
		std::scoped_lock _(g.mutex);
		auto deleter = [](void* p) { delete static_cast<std::move_only_function<bool(T&)>*>(p); };
		EventSystem::deletion_queue.emplace_back((*it).second, deleter);
		g.cached = false;
		g.callbacks.erase(it);
	}
}

template<typename T>
void Event<T>::notify() noexcept {
	ZoneScoped;

	ensureRegistered();

	auto& g = EventSystem::event_data[typeid(T)];
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
		ZoneScopedN("Callback dispatch");

		bool handled = (*static_cast<callback_t*>(callback))(static_cast<T&>(*this));
		if (handled) {
			return;
		}
	}
}

template<typename T, typename... Args>
  requires std::is_base_of_v<_detail::IEvent, T>
void send(Args&&... args) noexcept {
	ZoneScoped;

	static_assert(std::is_constructible_v<T, Args...>, "Invalid Construtor For Type T");
	TOAST_INFO("Events", "Sending Event: {}", typeid(T).name());
	// Allocate and enqueue event
	{
		std::scoped_lock _(EventSystem::pool_mutex);
		void* memory = _detail::allocate(sizeof(T), alignof(T));
		assert(memory);
		_detail::IEvent* event = new (memory) T(std::forward<Args>(args)...);
#ifdef DEBUG
		// TODO: event->stacktrace = std::stacktrace::current(1);
#endif
	}
}

}
