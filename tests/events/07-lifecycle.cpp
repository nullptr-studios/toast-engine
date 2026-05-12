#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include "test_registry.hpp"

#include <cassert>

struct LifecycleEvent : event::Event<LifecycleEvent> { };

TOAST_TEST_NAMED("events", "events/07-lifecycle", test_events_07_lifecycle) {
	int called = 0;

	{
		event::Listener listener;
		listener.subscribe<LifecycleEvent>([&]() { called++; });

		event::send<LifecycleEvent>();
		event::pollEvents();
		assert(called == 1);
	}    // listener destroyed here, should unsubscribe

	event::send<LifecycleEvent>();
	event::pollEvents();
	assert(called == 1);    // should still be 1 if it unsubscribed correctly
}
