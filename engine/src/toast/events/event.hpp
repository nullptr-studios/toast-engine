/**
 * @file Event.hpp
 * @author Dante and Xein
 * @date 15/03/26
 *
 * @brief Derive your event type as `struct Derived : Event<Derived>`,
 *        call event::send<T>() to enqueue, and event::pollEvents() to dispatch
 */
#pragma once

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace event {

/**
 * @brief Dispatches all queued events to their registered callbacks in priority order
 * @note Not thread-safe; call only from the main or game-logic thread
 * @note Events sent during dispatch are deferred and processed on the next call
 */
void TOAST_API pollEvents() noexcept;

/// @internal
namespace _detail {
/// @internal
/// Event needs to inherit from IEvent so we get the vtable for notify
struct TOAST_API IEvent {
	virtual ~IEvent() = default;

#ifdef DEBUG
	// std::stacktrace stacktrace;
#endif

private:
	friend void TOAST_API event::pollEvents() noexcept;
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
/// Maps callbacks to specific priorities that define the execution order
/// If a callback returns @c true it consumes the event, preventing further propagation
///
/// @internal
///
/// We have a multimap<priority, callback> that maps callbacks to their priorities; when we subscribe we return
/// an iterator to that specific callback to the event::Listener, which is how unsubscribe works; it just
/// erases that iterator. std::multimap guarantees iterators are never invalidated on insert/erase
///
/// To keep it thread safe and leverage cache locality, every subscribe/unsubscribe marks the cache dirty;
/// on notify we rebuild the processing vector from the map and iterate that; this lets the user safely
/// add or remove callbacks from within a callback itself
///
template<typename T>
struct Event : _detail::IEvent {
	friend class Listener;
	friend class ThreadListener;
	friend struct EventSystem;
	friend T;
	/// @brief callbacks that return 'true' are consumed and do not propogate
	using callback_t = std::move_only_function<bool(T&)>;
	using iterator_t = std::multimap<char, void*, std::greater<>>::iterator;

private:
	Event() = default;

	static void ensureRegistered() noexcept;

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

/**
 * @brief Internal dispatch table for the event system
 *
 * One EventInfo entry exists per registered event type. Do not use this struct directly;
 * go through Listener::subscribe() to register callbacks and event::send() to enqueue events.
 */
struct TOAST_API EventSystem {
	/**
	 * @brief Per-type callback registry
	 *
	 * callbacks is a priority-sorted multimap from priority char to type-erased function pointer.
	 * processing is a flat snapshot rebuilt from callbacks when cached is false; rebuilding
	 * on-demand lets subscribers add or remove callbacks from within a callback without
	 * invalidating iteration.
	 */
	struct EventInfo {
		std::mutex mutex;
		std::multimap<char, void*, std::greater<>> callbacks;
		std::vector<void*> processing;
		bool cached = false;
	};

	/// Dispatch table keyed by event type; one entry per registered event type
	static std::unordered_map<std::type_index, EventInfo> event_data;

	/// Protects the event queue memory pool during concurrent sends
	static std::mutex pool_mutex;

	/// Type-erased unsubscribe functions keyed by event type; used by Listener to erase iterators without the concrete type
	static std::unordered_map<std::type_index, std::function<void(std::any)>> unsubscribe_map;

	/// Deferred-delete queue; callbacks that unsubscribed during pollEvents are freed here on the next call
	/// to avoid invalidating the processing vector mid-dispatch
	static std::vector<std::unique_ptr<void, void (*)(void*)>> deletion_queue;

	/// Protects the event queue memory pool during concurrent sends
	static std::mutex deletion_mutex;

	template<typename T>
	static void registerEvent() {
		static_assert(std::is_base_of_v<Event<T>, T>, "CONTRACT VIOLATION: You Must Inhert as 'struct Derived : Event<Derived>'");
		if (event_data.contains(typeid(T))) {
			// guard against cross-DLL double-registration; same type can be registered from multiple translation units on Windows
			return;
		}
		TOAST_INFO("Events", "Registering Event Type: {}", typeid(T).name());
		event_data.emplace( // this is goofy because eventinfo has a mutex inside it
		    std::piecewise_construct,
		    std::forward_as_tuple(typeid(T)),
		    std::forward_as_tuple()
		);

		EventSystem::unsubscribe_map.emplace(typeid(T), [](const std::any& iter) {
			auto it = std::any_cast<typename Event<T>::iterator_t>(iter);
			Event<T>::unsubscribe(it);
		});
	}
};

/**
 * @brief Specialization point that exposes a C++ event type over the C# FFI via protobuf
 *
 * Specialize this template next to your ProtoTraits and use TOAST_PROTO_EVENT() to register it.
 * The specialization must define:
 *   - using Proto: the generated protobuf message type
 *   - using Event: the C++ event type (usually the same as T)
 *   - toProto(const T&): converts a C++ event to its protobuf form for serialization
 *   - fromProto(const Proto&): reconstructs a C++ event from its protobuf form
 *
 * @tparam T The C++ event type; must also satisfy ExposedEvent after specialization
 * @see ExposedEvent, TOAST_PROTO_EVENT
 */
template<typename T>
struct ProtoTraits;

/**
 * @brief Satisfied when ProtoTraits<T> is fully specialized with both conversion functions
 *
 * Used as a constraint on registerProtoEvent<T>() and TOAST_PROTO_EVENT(). An event type
 * that satisfies ExposedEvent can be sent from C# and received in C++, and vice versa.
 */
template<typename T>
concept ExposedEvent = requires(const T& e, const typename ProtoTraits<T>::Proto& p) {
	typename ProtoTraits<T>::Proto;
	typename ProtoTraits<T>::Event;
	{ ProtoTraits<T>::toProto(e) } -> std::same_as<typename ProtoTraits<T>::Proto>;
	{ ProtoTraits<T>::fromProto(p) } -> std::same_as<typename ProtoTraits<T>::Event>;
};

// register all exposed events into a registry
void registerProtoEvents();

}

#include "event.inl"
