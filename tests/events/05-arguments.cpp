#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include "test_registry.hpp"

#include <cassert>
#include <string>
#include <utility>

struct ArgEvent : event::Event<ArgEvent> {
	int value;
	std::string message;

	ArgEvent(int v, std::string m) : value(v), message(std::move(m)) { }
};

TOAST_TEST_NAMED("events", "events/05-arguments", test_events_05_arguments) {
	event::Listener listener;
	int received_value = 0;
	std::string received_message;

	listener.subscribe<ArgEvent>([&](ArgEvent& e) {
		received_value = e.value;
		received_message = e.message;
	});

	event::send<ArgEvent>(42, "hello event");
	event::send<ArgEvent>(ArgEvent(42, "hello event"));
	event::pollEvents();

	assert(received_value == 42);
	assert(received_message == "hello event");
}
