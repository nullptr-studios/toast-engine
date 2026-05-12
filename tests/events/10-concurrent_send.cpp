#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"

#include "test_registry.hpp"

#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

struct CountEvent : event::Event<CountEvent> { };

TOAST_TEST_NAMED("events", "events/10-concurrent_send", test_events_10_concurrent_send) {
	event::Listener listener;
	std::atomic<int> counter {0};

	listener.subscribe<CountEvent>([&]() { counter++; });

	constexpr int num_threads = 10;
	constexpr int events_per_thread = 1000;
	std::vector<std::thread> threads;

	threads.reserve(num_threads);
	for (int i = 0; i < num_threads; ++i) {
		threads.emplace_back([&]() {
			for (int j = 0; j < events_per_thread; ++j) {
				event::send<CountEvent>();
			}
		});
	}

	for (auto& t : threads) {
		t.join();
	}

	// Since it's double buffered, we might need two polls to get everything?
	// Actually, Send always goes to the current index.
	// PollEvents swaps the index first, then processes the one that was just current.
	// So one poll should get all of them if they were sent before PollEvents started.
	event::pollEvents();

	assert(counter == num_threads * events_per_thread);
}
