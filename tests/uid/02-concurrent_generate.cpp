#include "test_registry.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <toast/uid.hpp>
#include <unordered_set>
#include <vector>

// Nodes get their UID on whichever thread creates them, so threads that generate within the same clock tick must still
// get different numbers
TOAST_TEST_NAMED("uid", "uid/02-concurrent_generate", test_uid_02) {
	constexpr size_t thread_count = 8;
	constexpr size_t per_thread = 20000;

	std::vector<std::vector<uint64_t>> made(thread_count);
	std::vector<std::thread> threads;
	for (size_t t = 0; t < thread_count; ++t) {
		threads.emplace_back([&made, t] {
			made[t].reserve(per_thread);
			for (size_t i = 0; i < per_thread; ++i) {
				made[t].push_back(toast::UID::make().data());
			}
		});
	}
	for (auto& thread : threads) {
		thread.join();
	}

	std::unordered_set<uint64_t> seen;
	for (const auto& list : made) {
		seen.insert(list.begin(), list.end());
	}
	assert(seen.size() == thread_count * per_thread);
}
