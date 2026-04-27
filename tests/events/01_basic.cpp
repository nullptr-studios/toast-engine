#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <cassert>

struct BasicEvent : event::Event<BasicEvent> { };

auto main() -> int {
	event::Listener listener;
	bool called = false;
	listener.subscribe<BasicEvent>([&](BasicEvent& e) {
		std::print("{}", e.stacktrace);
		called = true;
	});

	event::send<BasicEvent>();
	event::pollEvents();

	assert(called);
	return 0;
}
