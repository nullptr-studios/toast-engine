/**
 * @file Event.hpp
 * @author Dante and Xein
 * @date 15/03/26
 *
 * @brief
 * - Derive your event type as `struct Derived : Event<Derived>`.
 * - Call event::Send<T>(...) to enqueue events
 * - Call event::PollEvents() to dispatch them
 */
#pragma once

#include "toast/log.hpp"

#include <any>
#include <cassert>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stacktrace>
#include <toast/export.hpp>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace event {

struct EventSystem;    // Forward declaration

/// @brief dispatches all queued events to their callbacks
/// @note Not thread-safe. Should be only ever called from a single thread / main thread
void TOAST_API pollEvents() noexcept;

/// @internal
namespace _detail {
/// @internal
/// Event needs to inherit from IEvent so we get the vtable for notify
struct TOAST_API IEvent {
	virtual ~IEvent() = default;

#ifdef DEBUG
	std::stacktrace stacktrace;
#endif

private:
	friend void event::pollEvents() noexcept;
	virtual void notify() noexcept = 0;
};

/// @brief allocates memory in the event queue pool
auto TOAST_API allocate(std::size_t size, std::size_t align) noexcept -> void*;
}

/// @brief queue's up an event
/// @note events sent during a PollEvents will be send on the next PollEvents
/// @param args arguments for the constructor
template<typename T, typename... Args>
  requires std::is_base_of_v<_detail::IEvent, T>
void send(Args&&... args) noexcept;

/// @class Event
/// @brief Base class for all events. Derive your event type as `struct Derived : Event<Derived>`.
///
/// @details
///
/// Maps callbacks to specfic prioritys that define the execution order of the callback
/// If a callback returns @c true it consumes the event, preventing further propagation
///
/// @internal
///
/// We have a multimap<priority, callback>> that maps callbacks to their prioritys when we subscribe a callback we return
/// a iterator to that specfic callback to the event::Listener this way we implment the unsubscribe function so that it
/// takes that iterator and removes it from the map. We can do this because std::multimap does not invalidate iterators
/// on insert/erase
///
/// To keep it thread safe as well as leverage cache locality every time we subscribe or unsubscribe a callback we
/// mark the cache as dirty and on notify we cache the callbacks into a std::vector processing and iterate through that to
/// run the callbacks
///
/// this allows us to keep the callback list thread safe as well as allow the user to remove and add more callbacks during
/// the callback itself
///
template<typename T>
struct Event : _detail::IEvent {
	friend class Listener;
	friend struct EventSystem;
	/// @brief callbacks that return 'true' are consumed and do not propogate
	using callback_t = std::move_only_function<bool(T&)>;
	using iterator_t = std::multimap<char, void*, std::greater<>>::iterator;

private:
	static inline struct Registrar {
		Registrar();
		bool registered;
	} registrar;

	/// @brief registers a callback to the event callbacks
	/// @param priority higher number callbacks first
	/// @param callback
	static auto subscribe(char priority, callback_t&& callback) noexcept -> iterator_t;

	/// @brief Removes a previously registered callback
	/// @param it iterator to the callback the function will remove
	static void unsubscribe(iterator_t it) noexcept;

	/// @brief executes all of the callbacks in order from highest priority to lowest
	void notify() noexcept override;
};

struct TOAST_API EventSystem {
	struct EventInfo {
		std::mutex mutex;
		std::multimap<char, void*, std::greater<>> callbacks;
		std::vector<void*> processing;
		bool cached = false;
	};

	/// @brief callback map for each type of event
	static std::unordered_map<std::type_index, EventInfo> event_data;

	/// @brief mutex for the event system pool
	static std::mutex pool_mutex;

	/// @brief vtable for unsubscribing callbacks
	static std::unordered_map<std::type_index, std::function<void(std::any)>> unsubscribe_map;

	/// @brief callbacks will be cleaned up only before pollEvents
	/// @note unique pointers have the option of storing a pointer to the function that deletes the object
	/// so thats how i implement the deletion_queue
	static std::vector<std::unique_ptr<void, void (*)(void*)>> deletion_queue;

	template<typename T>
	static void registerEvent() {
		static_assert(std::is_base_of_v<Event<T>, T>, "CONTRACT VIOLATION: You Must Inhert as 'struct Derived : Event<Derived>'");
		if (event_data.contains(typeid(T))) {
			TOAST_INFO(_detail::IEvent, "Registered Again Because Windows Is Shit", typeid(T).name());
			return;
		}
		TOAST_INFO(_detail::IEvent, "Registering Event Type: {}", typeid(T).name());
		event_data.emplace(
		    std::piecewise_construct,
		    std::forward_as_tuple(typeid(T)),
		    std::forward_as_tuple()    // This calls the EventInfo() constructor
		);

		EventSystem::unsubscribe_map.emplace(typeid(T), [](const std::any& iter) {
			auto it = std::any_cast<typename Event<T>::iterator_t>(iter);
			Event<T>::unsubscribe(it);
		});
	}
};

}

#include "event.inl"
