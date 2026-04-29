#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <cassert>

struct ConsumeEvent : event::Event<ConsumeEvent> { };

auto main() -> int {
	event::Listener listener;
	bool high_called = false;
	bool low_called = false;

	listener.subscribe<ConsumeEvent>(
	    [&]() {
		    high_called = true;
		    return true;    // Consume event
	    },
	    10
	);

	listener.subscribe<ConsumeEvent>([&]() { low_called = true; }, 0);

	event::send<ConsumeEvent>();
	event::pollEvents();

	assert(high_called);
	assert(!low_called);

	return 0;
}
