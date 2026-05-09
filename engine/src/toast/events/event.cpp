#include "event.hpp"
#include "toast/events/event_system.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <toast/log.hpp>
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
}

namespace _detail {

auto allocate(std::size_t size, std::size_t align) noexcept -> void* {
	void* mem = pools[index].pool.allocate(size, align);
	pools[index].queue.push_back(reinterpret_cast<IEvent*>(mem));
	return mem;
}

}

void pollEvents() noexcept {
	TOAST_INFO(_detail::IEvent, "Polling Events");

	// delete callbacks
	EventSystem::deletion_queue.clear();

	// swap memory pool
	uint32_t idx;
	{
		std::scoped_lock _(EventSystem::pool_mutex);
		idx = index;
		index = (index + 1) % pools.size();
	}

	// notify events
	for (auto& event : pools[idx].queue) {
		event->notify();
		std::destroy_at(event);
	}

	// reset memory pool
	pools[idx].queue.clear();
	pools[idx].pool.release();
}
}
