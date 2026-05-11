#include "event.hpp"

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

auto& pools() {
	static auto* value = new std::array<Pool, 2>();
	return *value;
}

auto& index() {
	static auto* value = new uint8_t{0};
	return *value;
}
}

namespace _detail {

auto allocate(std::size_t size, std::size_t align) noexcept -> void* {
	auto& pool_set = pools();
	void* mem = pool_set[index()].pool.allocate(size, align);
	pool_set[index()].queue.push_back(reinterpret_cast<IEvent*>(mem));
	return mem;
}

}

void pollEvents() noexcept {
	TOAST_INFO(_detail::IEvent, "Polling Events");

	// delete callbacks
	EventSystem::deletion_queue().clear();

	// swap memory pool
	uint32_t idx;
	{
		std::scoped_lock _(EventSystem::pool_mutex());
		auto& pool_index = index();
		idx = pool_index;
		pool_index = (pool_index + 1) % pools().size();
	}

	// notify events
	auto& pool_set = pools();
	for (auto& event : pool_set[idx].queue) {
		event->notify();
		std::destroy_at(event);
	}

	// reset memory pool
	pool_set[idx].queue.clear();
	pool_set[idx].pool.release();
}
}
