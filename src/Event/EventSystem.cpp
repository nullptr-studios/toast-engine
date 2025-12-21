/// @file   EventSystem.cpp
/// @author Xein

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Profiler.hpp>
#include <Engine/Event/Event.hpp>
#include <Engine/Event/EventSystem.hpp>

namespace event {

EventSystem* EventSystem::m_instance = nullptr;

EventSystem::EventSystem() {
	if (m_instance) {
		throw ToastException("EventSystem already exists");
	}
	TOAST_INFO("Initializing Event system");
	m_instance = this;
}

EventSystem::~EventSystem() {
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		if (!m_eventQueue.empty()) {
			TOAST_WARN("Event system was deleted with events on the queue");
		}
	}
	m_instance = nullptr;
}

void EventSystem::SendEvent(IEvent* event) {
	if (!event) {
		TOAST_WARN("Event is not valid, aborting send");
		return;
	}

	if (!m_instance) {
		throw ToastException("EventSystem not initialized");
	}
	// TOAST_TRACE("Event {0} sent", typeid(*event).name());
	{
		std::lock_guard<std::mutex> lock(m_instance->m_queueMutex);
		m_instance->m_eventQueue.push(event);
	}
}

void Send(IEvent* event) {
	EventSystem::SendEvent(event);
}

void EventSystem::PollEvents() {
	PROFILE_ZONE_NS("EventSystem::PollEvents()", 5);

	// Swap queue into a local queue under lock to allow other threads to push while
	// we process events without holding the global lock
	std::queue<IEvent*> local_queue;
	{
		std::lock_guard<std::mutex> lock(m_instance->m_queueMutex);
		if (m_instance->m_eventQueue.empty()) {
			return;
		}
		local_queue.swap(m_instance->m_eventQueue);
	}

	while (!local_queue.empty()) {
		IEvent* event = local_queue.front();
		local_queue.pop();
		if (!event) {
			continue;
		}

		event->Notify();
		delete event;
	}
}

}
