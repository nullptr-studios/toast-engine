#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <cassert>

struct LifecycleEvent : event::Event<LifecycleEvent> { };

auto main() -> int {
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

	return 0;
}
