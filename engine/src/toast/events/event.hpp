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
#include <unordered_map>
#include <vector>

namespace event {

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

/// @brief mutex for the event system
inline std::mutex mutex;

/// @brief vtable for unsubscribing callbacks
inline std::unordered_map<std::type_index, std::function<void(std::any)>> unsubscribe_map;

/// @brief callbacks will be cleaned up only before pollEvents
inline std::vector<std::unique_ptr<void, void (*)(void*)>> deletion_queue;

/// @brief allocates memory in the event queue pool
auto allocate(std::size_t size, std::size_t align) noexcept -> void*;
}

/// @brief queue's up an event
/// @note events sent during a PollEvents will be send on the next PollEvents
/// @param args arguments for the constructor
template<typename T, typename... Args>
  requires std::is_base_of_v<_detail::IEvent, T>
void TOAST_API send(Args&&... args) noexcept;

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
struct TOAST_API Event : _detail::IEvent {
	friend class Listener;
	/// @brief callbacks that return 'true' are consumed and do not propogate
	using callback_t = std::move_only_function<bool(T&)>;
	using iterator_t = std::multimap<char, callback_t*>::iterator;

private:
	static inline struct G {
		G() noexcept;
		std::multimap<char, callback_t*, std::greater<char>> callbacks;
		std::mutex mutex;
		std::vector<callback_t*> processing;
		bool cached;
	} g;

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

}

#include "event.inl"
