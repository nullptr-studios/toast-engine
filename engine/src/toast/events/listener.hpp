/**
 * @file Listener.hpp
 * @author Dante and Xein
 * @date 15/03/26
 */
#pragma once

#include "event.hpp"

#include <any>
#include <cassert>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace event {

/// @brief valid callbacks you can subscribe
/// @note if you dont return a boolean it will return false be default
template<typename F, typename TEvent>
concept EventCallback = std::is_invocable_r_v<bool, F, TEvent&> ||    // Returns bool(T&)
                        std::is_invocable_r_v<void, F, TEvent&> ||    // Returns void(T&)
                        std::is_invocable_r_v<bool, F> ||             // Returns bool()
                        std::is_invocable_r_v<void, F>;               // Returns void()

/// @class Listener
/// @brief manages the lifetime, subscription, and unsubscription of callbacks
///
/// @internal
///
/// we map Event<T>::iterator_t to types and names in the Listener class so that we can
/// manage the lifetime of the callbacks we subscribe
///
/// callbacks are allowed to be incomplete so that you dont have to return a boolean or
/// have the event as a parameter if you dont care about that
///
class TOAST_API Listener {
	struct Handle {
		std::type_index type;
		std::string name;
		std::any iterator;
	};

	struct {
		std::vector<Handle> callbacks;
		bool enabled;
	} m;

public:
	Listener();

	Listener(bool state);

	~Listener();

	/// @brief unsubscribes every event in type TEvent with that name
	/// @param name event callback name/s to remove
	template<typename TEvent>
	void unsubscribe(std::string_view name) noexcept;

	/// @brief subscribes a callback to an TEvent
	/// @param name of the callback
	/// @param callback
	/// @param priority (higher priority happens first)
	template<typename TEvent, EventCallback<TEvent&> F>
	void subscribe(std::string name, F&& callback, char priority = 0) noexcept;

	/// @brief subscribes a callback to an TEvent (name will be set to "unnamed")
	/// @param callback
	/// @param priority (higher priority happens first)
	template<typename TEvent, EventCallback<TEvent&> F>
	void subscribe(F&& callback, char priority = 0) noexcept;

	/// @brief enables/disables the callbacks in the listener
	void enabled(bool state);

	/// @return enabled state
	[[nodiscard]]
	auto enabled() const -> bool;
};
}

#include "listener.inl"
