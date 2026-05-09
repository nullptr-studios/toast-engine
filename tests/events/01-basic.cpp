#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"
#include "toast/events/event_system.hpp"

#include <cassert>

struct BasicEvent : event::Event<BasicEvent> { };

auto main() -> int {
	event::Listener listener;
	bool called = false;
	listener.subscribe<BasicEvent>([&called](BasicEvent& e) { called = true; });

	event::send<BasicEvent>();
	event::send<Test>();
	event::pollEvents();

	assert(called);
	return 0;
}
