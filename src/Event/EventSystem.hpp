/// @file   EventSystem.hpp
/// @author Xein
/// @date   16/04/25
/// @brief  Node responsible for handling the queue of events

#pragma once

#include <mutex>
#include <queue>

namespace event {
struct IEvent;
class ListenerSubNode;

class EventSystem {
public:
	friend class ListenerSubNode;

	EventSystem();
	~EventSystem();

	EventSystem(const EventSystem&) = delete;
	EventSystem& operator=(const EventSystem&) = delete;

	/// @brief See @c event::Send instead
	static void SendEvent(IEvent* event);

	void PollEvents();

private:
	static EventSystem* m_instance;
	std::queue<IEvent*> m_eventQueue;
	std::mutex m_queueMutex;
};

}
