#include "event.hpp"

#include "proto_event.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <string>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <vector>

namespace event {

std::unordered_map<std::type_index, EventSystem::EventInfo> EventSystem::event_data;
std::mutex EventSystem::pool_mutex;
std::unordered_map<std::type_index, std::function<void(std::any)>> EventSystem::unsubscribe_map;
std::vector<std::unique_ptr<void, void (*)(void*)>> EventSystem::deletion_queue;

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
	ZoneScoped;

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

namespace {
std::mutex ffi_mutex;
std::map<std::string, _detail::ProtoEntry, std::less<>> ffi_registry;
std::vector<const std::string*> names;
std::map<uint32_t, std::unique_ptr<Listener>> listeners;
std::atomic<uint32_t> next_listener = 1;
}

namespace _detail {

void registerProtoEntry(std::string name, ProtoEntry entry) {
	std::scoped_lock lock {ffi_mutex};
	auto [it, inserted] = ffi_registry.emplace(std::move(name), entry);
	if (inserted) {
		names.push_back(&it->first);
	}
}

}

void registerProtoEvents() {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;

	// Every TOAST_PROTO_EVENT(...) pushed a registrar at static-init
	for (auto registrar : _detail::protoRegistrars()) {
		registrar();
	}
}

}

extern "C" {

auto events_listener_create(void) -> uint32_t {
	auto listener = std::make_unique<event::Listener>();
	uint32_t handle = event::next_listener.fetch_add(1);
	std::scoped_lock lock(event::ffi_mutex);
	event::listeners.emplace(handle, std::move(listener));
	return handle;
}

void events_listener_destroy(uint32_t handle) {
	std::scoped_lock lock(event::ffi_mutex);
	event::listeners.erase(handle);
}

auto events_listener_subscribe(uint32_t handle, const char* name, event_callback callback, void* user_data, char priority)
    -> int {
	if (!name || !callback) {
		return -1;
	}
	std::scoped_lock lock(event::ffi_mutex);
	auto lit = event::listeners.find(handle);
	if (lit == event::listeners.end()) {
		return -1;
	}
	auto eit = event::ffi_registry.find(name);
	if (eit == event::ffi_registry.end()) {
		return -1;
	}
	// Subscribe under the lock so the listener can't be destroyed mid-call
	eit->second.subscribe(*lit->second, callback, user_data, priority, name);
	return 0;
}

void events_listener_unsubscribe(uint32_t handle, const char* name) {
	if (!name) {
		return;
	}
	std::scoped_lock lock(event::ffi_mutex);
	auto lit = event::listeners.find(handle);
	if (lit == event::listeners.end()) {
		return;
	}
	auto eit = event::ffi_registry.find(name);
	if (eit == event::ffi_registry.end()) {
		return;
	}
	eit->second.unsubscribe(*lit->second, name);
}

auto events_send(const char* name, const uint8_t* data, uint32_t size) -> int {
	if (!name) {
		return -1;
	}
	event::_detail::ProtoEntry entry;
	{
		std::scoped_lock lock(event::ffi_mutex);
		auto it = event::ffi_registry.find(name);
		if (it == event::ffi_registry.end()) {
			return -1;
		}
		entry = it->second;
	}
	return entry.send(data, size);    // enqueue outside the lock
}

auto events_count(void) -> uint32_t {
	std::scoped_lock lock(event::ffi_mutex);
	return static_cast<uint32_t>(event::names.size());
}

auto events_name(uint32_t index) -> const char* {
	std::scoped_lock lock(event::ffi_mutex);
	if (index >= event::names.size()) {
		return nullptr;
	}
	return event::names[index]->c_str();
}
}
