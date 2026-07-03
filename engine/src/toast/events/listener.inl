#pragma once
#include "listener.hpp"
#include "toast/log.hpp"

#include <utility>

namespace event {

template<typename TEvent>
void Listener::unsubscribe(std::string_view name) noexcept {
	TOAST_TRACE("Events", "Unsubscribing Callback {} to {}", name, typeid(TEvent).name());
	std::erase_if(m.callbacks, [&](auto& callback) {
		auto& [type, sub_name, any] = callback;
		if (typeid(TEvent) == type && name == sub_name) {
			auto any_val = std::any_cast<typename Event<TEvent>::iterator_t>(any);
			Event<TEvent>::unsubscribe(any_val);
			return true;
		}
		return false;
	});
}

template<typename TEvent, EventCallback<TEvent&> F>
void Listener::subscribe(std::string name, F&& callback, char priority) noexcept {
	TOAST_TRACE("Events", "Subscribing Callback {} to {}", name, typeid(TEvent).name());
	auto wrapper = [fn = std::forward<F>(callback), this](TEvent& e) mutable -> bool {
		// if (not m.enabled) {
		//	return false; TODO: This can crash right now since the callback can live longer than the listener and will crash when
		// m.enabled is deleted memory
		// }
		if constexpr (std::is_invocable_r_v<bool, F, TEvent&>) {    // Returns bool(T&)
			return std::invoke(fn, e);
		} else if constexpr (std::is_invocable_v<F, TEvent&>) {     // Returns void(T&)
			std::invoke(fn, e);
			return false;
		} else if constexpr (std::is_invocable_r_v<bool, F>) {      // Returns bool()
			return std::invoke(fn);
		} else {                                                    // Returns void()
			std::invoke(fn);
			return false;
		}
	};

	auto it = Event<TEvent>::subscribe(priority, std::move(wrapper));
	m.callbacks.push_back({typeid(TEvent), std::move(name), it});
}

template<typename TEvent, EventCallback<TEvent&> F>
void Listener::subscribe(F&& callback, char priority) noexcept {
	subscribe<TEvent>("unnamed", std::forward<F>(callback), priority);
}

}
