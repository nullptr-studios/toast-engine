#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <cassert>
#include <string>

struct NamedEvent : event::Event<NamedEvent> { };

auto main() -> int {
	event::Listener listener;
	int called = 0;

	listener.subscribe<NamedEvent>("test_callback", [&](NamedEvent&) { called++; });

	event::send<NamedEvent>();
	event::pollEvents();
	assert(called == 1);

	listener.unsubscribe<NamedEvent>("test_callback");

	event::send<NamedEvent>();
	event::pollEvents();
	assert(called == 1);    // Should still be 1 because it's unsubscribed

	return 0;
}
