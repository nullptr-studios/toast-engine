#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <cassert>

struct EventOne : event::Event<EventOne> { };

struct EventTwo : event::Event<EventTwo> { };

auto main() -> int {
	event::Listener listener_a;
	event::Listener listener_b;
	int a_calls = 0;
	int b_calls = 0;

	listener_a.subscribe<EventOne>([&]() { a_calls++; });
	listener_b.subscribe<EventTwo>([&]() { b_calls++; });

	event::send<EventOne>();
	event::send<EventTwo>();
	event::pollEvents();

	assert(a_calls == 1);
	assert(b_calls == 1);

	return 0;
}
