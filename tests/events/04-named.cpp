#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include "test_registry.hpp"

#include <cassert>

struct NamedEvent : event::Event<NamedEvent> { };

TOAST_TEST_NAMED("events", "events/04-named", test_events_04_named) {
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
}
