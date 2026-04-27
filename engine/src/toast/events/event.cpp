// TODO: implement Circular buffer
#include "event.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <print>
#include <vector>

namespace event {

namespace {
struct Pool {
	alignas(64) std::array<std::byte, 1024> buffer;
	std::pmr::monotonic_buffer_resource pool;
	std::vector<_detail::IEvent*> queue;

	Pool() : pool(buffer.data(), buffer.size()) { }
};

std::array<Pool, 2> pools;
uint8_t index = 0;
std::mutex poll_mutex;

}

namespace _detail {

auto allocate(std::size_t size, std::size_t align) noexcept -> void* {
	void* mem = pools[index].pool.allocate(size, align);
	pools[index].queue.push_back(reinterpret_cast<IEvent*>(mem));
	return mem;
}

}

void pollEvents() noexcept {
	std::scoped_lock lock(poll_mutex);
	uint32_t idx;
	{
		std::scoped_lock _(_detail::mutex);
		idx = index;
		index = (index + 1) % pools.size();
	}

	if (not pools[idx].queue.empty()) {
		std::println("Polling Events");
	}
	for (auto& event : pools[idx].queue) {
		event->notify();
		std::destroy_at(event);
	}

	pools[idx].queue.clear();
	pools[idx].pool.release();
}
}
