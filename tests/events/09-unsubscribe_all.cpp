#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include "test_registry.hpp"

#include <cassert>

struct UnsubEvent : event::Event<UnsubEvent> { };

TOAST_TEST_NAMED("events", "events/09-unsubscribe_all", test_events_09_unsubscribe_all) {
	event::Listener listener;
	int called = 0;

	listener.subscribe<UnsubEvent>("test", [&]() { called++; });
	listener.subscribe<UnsubEvent>("test", [&]() { called++; });

	listener.unsubscribe<UnsubEvent>("test");

	event::send<UnsubEvent>();
	event::pollEvents();

	assert(called == 0);
}
