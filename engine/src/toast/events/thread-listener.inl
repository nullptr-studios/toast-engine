#pragma once
#include "thread-listener.hpp"
#include "toast/log.hpp"

#include <utility>

namespace event {

template<typename TEvent>
void ThreadListener::unsubscribe(std::string_view name) noexcept {
	TOAST_TRACE("Events", "Unsubscribing Callback {} to {}", name, typeid(TEvent).name());

	std::erase_if(m.callbacks, [&](auto& pair) {
		auto& [priority, handle] = pair;
		auto& [type, sub_name, cb] = handle;
		return typeid(TEvent) == type && name == sub_name;
	});
}

template<typename TEvent, EventCallback<TEvent&> F>
void ThreadListener::subscribe(std::string name, F&& callback, char priority) noexcept {
	TOAST_TRACE("Events", "Subscribing Callback {} to {}", name, typeid(TEvent).name());

	callback_t cb = [fn = std::forward<F>(callback), enabled = m.enabled](_detail::IEvent& e) mutable -> bool {
		if (not enabled->load()) {
			return false;
		}
		if constexpr (std::is_invocable_r_v<bool, F, TEvent&>) {    // Returns bool(T&)
			return std::invoke(fn, static_cast<TEvent&>(e));
		} else if constexpr (std::is_invocable_v<F, TEvent&>) {     // Returns void(T&)
			std::invoke(fn, static_cast<TEvent&>(e));
			return false;
		} else if constexpr (std::is_invocable_r_v<bool, F>) {      // Returns bool()
			return std::invoke(fn);
		} else {                                                    // Returns void()
			std::invoke(fn);
			return false;
		}
	};

	m.callbacks.emplace(priority, Handle {typeid(TEvent), std::move(name), std::move(cb)});
	listen<TEvent>();
}

template<typename TEvent, EventCallback<TEvent&> F>
void ThreadListener::subscribe(F&& callback, char priority) noexcept {
	subscribe<TEvent>("unnamed", std::forward<F>(callback), priority);
}

template<typename TEvent>
void ThreadListener::listen() noexcept {
	if (m.recievers.contains(typeid(TEvent))) {
		return;
	}

	auto receiver_cb = [this, enabled = m.enabled](TEvent& e) -> bool {
		if (not enabled->load()) {
			return false;
		}
		auto copy = new TEvent(e);
		{
			std::scoped_lock lock(m.queue_mutex);
			m.event_queue.push_back(copy);
		}
		return false;
	};

	auto it = Event<TEvent>::subscribe(0, std::move(receiver_cb));

	m.recievers.emplace(typeid(TEvent), it);
}

}
