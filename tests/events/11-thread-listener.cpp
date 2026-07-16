#include "toast/events/event.hpp"
#include "toast/events/thread-listener.hpp"

#include "test_registry.hpp"

#include <cassert>

struct BasicEvent : event::Event<BasicEvent> { };

TOAST_TEST_NAMED("events", "events/11-thread-listener", test_events_11_thread_listener) {
	event::ThreadListener listener;
	bool called = false;
	listener.subscribe<BasicEvent>([&called](BasicEvent& e) { called = true; });

	event::send<BasicEvent>();
	event::pollEvents();

	listener.pollEvents();

	assert(called);
}
