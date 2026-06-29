/**
 * @file ThreadListener.hpp
 * @author Dante and Xein
 * @date 15/03/26
 */
#pragma once

#include "listener.hpp"

#include <any>
#include <cassert>
#include <flat_map>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace event {

class TOAST_API ThreadListener {
	using callback_t = std::move_only_function<bool(_detail::IEvent&)>;

	struct Handle {
		std::type_index type;
		std::string name;
		callback_t callback;
	};

	struct {
		std::vector<_detail::IEvent*> event_queue;                     ///< @brief copied event queue
		std::mutex queue_mutex;
		std::map<std::type_index, std::any> recievers;                 ///< @brief callbacks that add events to the event_queue
		std::flat_multimap<char, Handle, std::greater<>> callbacks;    ///< @brief callbacks for the vents
		std::atomic<bool>* enabled;                                    ///< @brief enabled bool
	} m;

public:
	ThreadListener();
	ThreadListener(bool state);
	~ThreadListener();

	/// @brief clears away all callbacks
	void clear();

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

	/// @brief iterates through the event_queue and calls the callbacks
	/// @note this function must be ticked regularly
	void pollEvents();

private:
	/// @brief adds a callback to start listening for an event on the global event system
	template<typename TEvent>
	void listen() noexcept;
};
}

#include "thread-listener.inl"
