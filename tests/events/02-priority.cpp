#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include "test_registry.hpp"

#include <cassert>
#include <vector>

TOAST_TEST_NAMED("events", "events/02-priority", test_events_02_priority) {
	struct PriorityEvent : event::Event<PriorityEvent> { };

	event::Listener listener;
	std::vector<int> order;

	listener.subscribe<PriorityEvent>([&]() { order.push_back(1); }, 10);    // High priority

	listener.subscribe<PriorityEvent>([&]() { order.push_back(2); }, 0);     // Default priority

	listener.subscribe<PriorityEvent>([&]() { order.push_back(3); }, 20);    // Higher priority

	event::send<PriorityEvent>();
	event::pollEvents();

	assert(order.size() == 3);
	assert(order[0] == 3);
	assert(order[1] == 1);
	assert(order[2] == 2);
}
