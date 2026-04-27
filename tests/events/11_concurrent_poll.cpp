#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

struct PollEvent : event::Event<PollEvent> { };

auto main() -> int {
	event::Listener listener;
	std::atomic<int> counter {0};

	listener.subscribe<PollEvent>([&]() { counter++; });

	constexpr int num_events = 1000;
	for (int i = 0; i < num_events; ++i) {
		event::send<PollEvent>();
	}

	constexpr int num_poll_threads = 4;
	std::vector<std::thread> threads;

	threads.reserve(num_poll_threads);
	for (int i = 0; i < num_poll_threads; ++i) {
		threads.emplace_back([&]() {
			// Concurrent poll calls should be serialized by the lock
			event::pollEvents();
		});
	}

	for (auto& t : threads) {
		t.join();
	}

	// Since each poll might process a different buffer or return empty if nothing's there,
	// we just want to ensure it doesn't crash and that all events are eventually processed.
	// However, if we sent them all to the same buffer before polling started:
	// Thread 1: gets index 0, index becomes 1. Processes 1000 events.
	// Thread 2: gets index 1, index becomes 0. Processes empty buffer.
	// etc.
	// Total processed should still be the sum of what was in the buffers.

	// Check remaining buffer just in case
	event::pollEvents();

	assert(counter == num_events);

	return 0;
}
