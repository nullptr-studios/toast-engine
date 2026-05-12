#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include "test_registry.hpp"

#include <cassert>

struct BasicEvent : event::Event<BasicEvent> { };

TOAST_TEST_NAMED("events", "events/01-basic", test_events_01_basic) {
	event::Listener listener;
	bool called = false;
	listener.subscribe<BasicEvent>([&called](BasicEvent& e) { called = true; });

	event::send<BasicEvent>();
	event::pollEvents();

	assert(called);
}
