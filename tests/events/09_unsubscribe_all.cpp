#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <cassert>

struct UnsubEvent : event::Event<UnsubEvent> { };

auto main() -> int {
	event::Listener listener;
	int called = 0;

	listener.subscribe<UnsubEvent>("test", [&]() { called++; });
	listener.subscribe<UnsubEvent>("test", [&]() { called++; });

	listener.unsubscribe<UnsubEvent>("test");

	event::send<UnsubEvent>();
	event::pollEvents();

	assert(called == 0);

	return 0;
}
