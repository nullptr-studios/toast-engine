#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <cassert>

struct EventA : event::Event<EventA> { };

struct EventB : event::Event<EventB> { };

auto main() -> int {
	event::Listener listener;
	bool b_called = false;

	listener.subscribe<EventA>([]() { event::send<EventB>(); });

	listener.subscribe<EventB>([&]() { b_called = true; });

	event::send<EventA>();
	event::pollEvents();    // Dispatches event_a, which enqueues event_b
	assert(!b_called);      // event_b should not have been called yet

	event::pollEvents();    // Dispatches event_b
	assert(b_called);

	return 0;
}
