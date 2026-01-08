/// @file   Event.hpp
/// @author Xein
/// @date   14/04/25
/// @brief  Base Event class

#pragma once
#include "ListenerComponent.hpp"

namespace event {

using EventMap = std::multimap<unsigned char, ListenerComponent*>;

/// @brief  Core components of the event struct
struct IEvent {
	friend class EventSystem;
	virtual ~IEvent() = default;

protected:
	virtual void Notify() = 0;
};

/// @brief  Base class for all the Events
/// @tparam EventType Template for the CRTP (more info below)
template<typename EventType>
struct Event : IEvent {
	friend class EventSystem;

	/// @brief Holds all the listeners that are subscribed to the events
	static EventMap subscribers;

protected:
	void Notify() override;
};

template<typename EventType>
EventMap Event<EventType>::subscribers;

template<typename EventType>
void Event<EventType>::Notify() {
	bool handled = false;

	// Don't hold the lock during dispatch to prevent deadlocks
	std::vector<ListenerComponent*> listeners_copy;
	{
		std::lock_guard<std::mutex> lock(s_eventMutex);
		listeners_copy.reserve(subscribers.size());
		for (auto it = subscribers.rbegin(); it != subscribers.rend(); ++it) {
			listeners_copy.push_back(it->second);
		}
	}

	// Dispatch to all listeners, without holding the lock
	for (auto* listener : listeners_copy) {
		if (!listener) {
			continue;    // Listener might have been destroyed after we copied
		}
		handled = listener->Dispatch<EventType>(dynamic_cast<EventType*>(this));
		if (handled) {
			return;
		}
	}
}

}
