/**
 * @file heap_tracking.cpp
 * @author Nullptr* Studios
 * @date 2026-11-02
 */

// Replaces the global allocation functions of toast_engine so Tracy sees every C++ heap allocation
// Everything forwards to malloc and free so memory still crosses DLL boundaries safely
#if defined(TRACY_ENABLE) && defined(TOAST_TRACK_HEAP)

#include <atomic>
#include <cstdint>
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#endif
#include <cstring>
#include <mutex>
#include <new>
#include <tracy/Tracy.hpp>

#ifndef TOAST_HEAP_CALLSTACK_DEPTH
#define TOAST_HEAP_CALLSTACK_DEPTH 0
#endif

namespace {

/// Addresses Tracy currently believes are live
/// Other modules free engine blocks with their own delete, and Tracy kills the session when an address is allocated twice or
/// freed untracked Storage comes from malloc since operator new would recurse
class LiveSet {
public:
	/// False when the address was already present
	auto insert(std::uintptr_t key, std::uint32_t epoch) -> bool {
		std::scoped_lock lock(m_mutex);
		sync(epoch);
		if (m_count * 2 >= m_capacity && not grow()) {
			return true;
		}
		const std::size_t mask = m_capacity - 1;
		for (std::size_t i = hash(key) & mask;; i = (i + 1) & mask) {
			if (m_slots[i] == key) {
				return false;
			}
			if (m_slots[i] == 0) {
				m_slots[i] = key;
				++m_count;
				return true;
			}
		}
	}

	auto erase(std::uintptr_t key, std::uint32_t epoch) -> bool {
		std::scoped_lock lock(m_mutex);
		sync(epoch);
		if (m_count == 0) {
			return false;
		}
		const std::size_t mask = m_capacity - 1;
		std::size_t i = hash(key) & mask;
		while (m_slots[i] != key) {
			if (m_slots[i] == 0) {
				return false;
			}
			i = (i + 1) & mask;
		}
		// backward shift so lookups never need tombstones
		for (std::size_t j = i;;) {
			j = (j + 1) & mask;
			if (m_slots[j] == 0) {
				break;
			}
			const std::size_t home = hash(m_slots[j]) & mask;
			if (const bool movable = i <= j ? (home <= i || home > j) : (home <= i && home > j)) {
				m_slots[i] = m_slots[j];
				i = j;
			}
		}
		m_slots[i] = 0;
		--m_count;
		return true;
	}

private:
	static auto hash(std::uintptr_t key) -> std::size_t {
		return static_cast<std::size_t>((key >> 4) * 0x9E3779B97F4A7C15ull >> 20);
	}

	/// A new Tracy session starts from nothing
	void sync(std::uint32_t epoch) {
		if (epoch != m_epoch) {
			m_epoch = epoch;
			m_count = 0;
			if (m_slots != nullptr) {
				std::memset(m_slots, 0, m_capacity * sizeof(std::uintptr_t));
			}
		}
	}

	auto grow() -> bool {
		const std::size_t capacity = m_capacity == 0 ? 1024 : m_capacity * 2;
		auto* slots = static_cast<std::uintptr_t*>(std::calloc(capacity, sizeof(std::uintptr_t)));    // NOLINT(*-no-malloc)
		if (slots == nullptr) {
			return false;
		}
		const std::size_t mask = capacity - 1;
		for (std::size_t i = 0; i < m_capacity; ++i) {
			if (m_slots[i] == 0) {
				continue;
			}
			std::size_t j = hash(m_slots[i]) & mask;
			while (slots[j] != 0) {
				j = (j + 1) & mask;
			}
			slots[j] = m_slots[i];
		}
		std::free(m_slots);    // NOLINT(*-no-malloc)
		m_slots = slots;
		m_capacity = capacity;
		return true;
	}

	std::mutex m_mutex;
	std::uintptr_t* m_slots = nullptr;
	std::size_t m_capacity = 0;
	std::size_t m_count = 0;
	std::uint32_t m_epoch = 0;
};

constexpr std::size_t k_shard_count = 64;

struct alignas(64) Shard {
	LiveSet set;
};

auto shards() -> Shard* {
	static Shard table[k_shard_count];    // NOLINT(*-avoid-c-arrays)
	return table;
}

auto shardFor(void* ptr) -> LiveSet& {
	return shards()[((reinterpret_cast<std::uintptr_t>(ptr) >> 4) * 0x9E3779B97F4A7C15ull >> 58) % k_shard_count].set;
}

std::atomic<std::uint32_t> g_epoch {1};
std::atomic<bool> g_was_connected {false};

/// Bumps the epoch when a session ends so the next one starts with empty tables, false while nobody listens
auto sessionActive() -> bool {
	if (TracyIsConnected) {
		if (not g_was_connected.load(std::memory_order_relaxed)) {
			g_was_connected.store(true, std::memory_order_relaxed);
		}
		return true;
	}
	if (g_was_connected.exchange(false, std::memory_order_relaxed)) {
		g_epoch.fetch_add(1, std::memory_order_relaxed);
	}
	return false;
}

void reportFree(void* ptr) {
#if TOAST_HEAP_CALLSTACK_DEPTH > 0
	TracyFreeS(ptr, TOAST_HEAP_CALLSTACK_DEPTH);
#else
	TracyFree(ptr);
#endif
}

void trackAlloc(void* ptr, std::size_t size) {
	if (ptr == nullptr || not sessionActive()) {
		return;
	}
	const auto epoch = g_epoch.load(std::memory_order_relaxed);
	if (not shardFor(ptr).insert(reinterpret_cast<std::uintptr_t>(ptr), epoch)) {
		// freed by a module that does not report to Tracy
		reportFree(ptr);
	}
#if TOAST_HEAP_CALLSTACK_DEPTH > 0
	TracyAllocS(ptr, size, TOAST_HEAP_CALLSTACK_DEPTH);
#else
	TracyAlloc(ptr, size);
#endif
}

void trackFree(void* ptr) {
	if (ptr == nullptr || not sessionActive()) {
		return;
	}
	const auto epoch = g_epoch.load(std::memory_order_relaxed);
	if (shardFor(ptr).erase(reinterpret_cast<std::uintptr_t>(ptr), epoch)) {
		reportFree(ptr);
	}
}

auto allocate(std::size_t size) -> void* {
	return std::malloc(size != 0 ? size : 1);    // NOLINT(*-no-malloc)
}

auto allocateAligned(std::size_t size, std::size_t alignment) -> void* {
	if (size == 0) {
		size = 1;
	}
#ifdef _WIN32
	return _aligned_malloc(size, alignment);
#else
	// aligned_alloc wants a size that is a multiple of the alignment
	if (size > static_cast<std::size_t>(-1) - alignment + 1) {
		return nullptr;
	}
	return std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
}

void releaseAligned(void* ptr) {
#ifdef _WIN32
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif
}

auto allocateOrThrow(std::size_t size) -> void* {
	while (true) {
		if (void* ptr = allocate(size)) {
			trackAlloc(ptr, size);
			return ptr;
		}
		std::new_handler handler = std::get_new_handler();
		if (handler == nullptr) {
			throw std::bad_alloc();
		}
		handler();
	}
}

auto allocateOrThrowAligned(std::size_t size, std::align_val_t alignment) -> void* {
	while (true) {
		if (void* ptr = allocateAligned(size, static_cast<std::size_t>(alignment))) {
			trackAlloc(ptr, size);
			return ptr;
		}
		std::new_handler handler = std::get_new_handler();
		if (handler == nullptr) {
			throw std::bad_alloc();
		}
		handler();
	}
}

auto allocateNoThrow(std::size_t size) noexcept -> void* {
	try {
		return allocateOrThrow(size);
	} catch (...) { return nullptr; }
}

auto allocateNoThrowAligned(std::size_t size, std::align_val_t alignment) noexcept -> void* {
	try {
		return allocateOrThrowAligned(size, alignment);
	} catch (...) { return nullptr; }
}

void release(void* ptr) noexcept {
	trackFree(ptr);
	std::free(ptr);    // NOLINT(*-no-malloc)
}

void releaseTrackedAligned(void* ptr) noexcept {
	trackFree(ptr);
	releaseAligned(ptr);
}

}    // namespace

auto operator new(std::size_t size) -> void* {
	return allocateOrThrow(size);
}

auto operator new[](std::size_t size) -> void* {
	return allocateOrThrow(size);
}

auto operator new(std::size_t size, const std::nothrow_t&) noexcept -> void* {
	return allocateNoThrow(size);
}

auto operator new[](std::size_t size, const std::nothrow_t&) noexcept -> void* {
	return allocateNoThrow(size);
}

auto operator new(std::size_t size, std::align_val_t alignment) -> void* {
	return allocateOrThrowAligned(size, alignment);
}

auto operator new[](std::size_t size, std::align_val_t alignment) -> void* {
	return allocateOrThrowAligned(size, alignment);
}

auto operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept -> void* {
	return allocateNoThrowAligned(size, alignment);
}

auto operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept -> void* {
	return allocateNoThrowAligned(size, alignment);
}

void operator delete(void* ptr) noexcept {
	release(ptr);
}

void operator delete[](void* ptr) noexcept {
	release(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
	release(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
	release(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
	release(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
	release(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
	releaseTrackedAligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
	releaseTrackedAligned(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
	releaseTrackedAligned(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
	releaseTrackedAligned(ptr);
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
	releaseTrackedAligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
	releaseTrackedAligned(ptr);
}

#endif
