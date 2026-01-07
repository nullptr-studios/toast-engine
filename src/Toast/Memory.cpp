#include "Toast/Memory.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h>    // posix_memalign
#endif
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

// NOLINTBEGIN
#ifdef _WIN32
namespace toast::memory {

// Pooled bucket sizes
static constexpr std::array<std::size_t, 8> K_BUCKET_SIZES { 32, 64, 128, 256, 512, 1024, 2048, 4096 };
inline constexpr std::size_t K_BUCKET_SIZES_SIZE = K_BUCKET_SIZES.size();
constexpr std::size_t K_LARGE_ALLOCATION_THRESHOLD = K_BUCKET_SIZES.back();

constexpr std::size_t KB = 1024ull;
constexpr std::size_t MB = KB * 1024ull;
constexpr std::size_t GB = MB * 1024ull;

// cap free nodes per bucket to avoid unlimited pooling
static constexpr std::size_t K_MAX_FREE_PER_BUCKET = KB * 16;    // 16K blocks per bucket max free

struct BlockHeader {                                             // stored at block start
	uint32_t bucketIndex;                                          // bucket or large sentinel
	std::size_t requestedSize;                                     // user bytes (wider)
	BlockHeader* next;                                             // freelist linkage when free
};

// Global stats
static std::atomic<std::size_t> g_currentBytes { 0 };
static std::atomic<std::size_t> g_peakBytes { 0 };
static std::atomic<std::size_t> g_largeBytes { 0 };
static std::atomic<std::size_t> g_largeCount { 0 };
static std::array<std::atomic<std::size_t>, K_BUCKET_SIZES_SIZE> g_bucketInUse {};    // active per bucket
static std::array<std::atomic<BlockHeader*>, K_BUCKET_SIZES_SIZE> g_freeHead {};      // freelist head per bucket (typed)
static std::array<std::atomic<std::size_t>, K_BUCKET_SIZES_SIZE> g_freeCount {};      // free nodes per bucket
static std::atomic<std::size_t> g_poolReservedBytes { 0 };

static constexpr uint32_t kLargeSentinel = 0xFFFFFFFFu;

static int BucketIndexFor(std::size_t total) noexcept {
	// binary search for first bucket >= total
	auto it = std::lower_bound(K_BUCKET_SIZES.begin(), K_BUCKET_SIZES.end(), total);
	if (it == K_BUCKET_SIZES.end()) {
		return -1;
	}
	return static_cast<int>(std::distance(K_BUCKET_SIZES.begin(), it));
}

static void PushFreeNode(int idx, BlockHeader* hdr) noexcept {
	// enforce a max cached nodes per bucket to avoid unbounded growth
	auto cnt = g_freeCount[idx].load(std::memory_order_relaxed);
	if (cnt >= K_MAX_FREE_PER_BUCKET) {
		// pool full: drop the node
		std::free(hdr);
		return;
	}
	// add into freelist
	BlockHeader* old = g_freeHead[idx].load(std::memory_order_relaxed);
	do {
		hdr->next = old;
	} while (!g_freeHead[idx].compare_exchange_weak(old, hdr, std::memory_order_release, std::memory_order_relaxed));
	g_freeCount[idx].fetch_add(1, std::memory_order_relaxed);
	// account reserved bytes for the free pool
	g_poolReservedBytes.fetch_add(K_BUCKET_SIZES[idx], std::memory_order_relaxed);
}

static BlockHeader* PopFreeNode(int idx) noexcept {
	BlockHeader* old = g_freeHead[idx].load(std::memory_order_acquire);
	while (old) {
		BlockHeader* hdr = old;
		BlockHeader* nxt = hdr->next;
		if (g_freeHead[idx].compare_exchange_weak(old, nxt, std::memory_order_acq_rel, std::memory_order_acquire)) {
			g_freeCount[idx].fetch_sub(1, std::memory_order_relaxed);
			// taken out of free pool, decrement reserved bytes
			g_poolReservedBytes.fetch_sub(K_BUCKET_SIZES[idx], std::memory_order_relaxed);
			return hdr;
		}
	}
	return nullptr;
}

void* Alloc(std::size_t size) noexcept {
	if (size == 0) {
		size = 1;
	}
	std::size_t total = size + sizeof(BlockHeader);
	int idx = BucketIndexFor(total);
	if (idx >= 0) {
		BlockHeader* hdr = PopFreeNode(idx);
		if (!hdr) {
			void* raw = std::malloc(K_BUCKET_SIZES[idx]);
			if (!raw) {
				return nullptr;
			}
			hdr = static_cast<BlockHeader*>(raw);
			hdr->bucketIndex = (uint32_t)idx;
			hdr->next = nullptr;
			// NOTE: do NOT count this allocation as poolReserved
		}
		hdr->requestedSize = size;
		g_bucketInUse[idx].fetch_add(1, std::memory_order_relaxed);
		auto cur = g_currentBytes.fetch_add(size, std::memory_order_relaxed) + size;
		auto peak = g_peakBytes.load(std::memory_order_relaxed);
		// stalls thread until we successfully update peak
		while (cur > peak && !g_peakBytes.compare_exchange_weak(peak, cur, std::memory_order_relaxed)) { }
		void* user = hdr + 1;
#ifdef TRACY_ENABLE
		TracyAlloc(user, size);
#endif
		return user;
	}

	// large allocation
	void* raw = std::malloc(total);
	if (!raw) {
		return nullptr;
	}
	auto* hdr = static_cast<BlockHeader*>(raw);
	hdr->bucketIndex = kLargeSentinel;
	hdr->requestedSize = size;
	hdr->next = nullptr;
	g_largeBytes.fetch_add(size, std::memory_order_relaxed);
	g_largeCount.fetch_add(1, std::memory_order_relaxed);
	auto cur = g_currentBytes.fetch_add(size, std::memory_order_relaxed) + size;
	auto peak = g_peakBytes.load(std::memory_order_relaxed);
	// stalls thread until we successfully update peak
	while (cur > peak && !g_peakBytes.compare_exchange_weak(peak, cur, std::memory_order_relaxed)) { }
	void* user = hdr + 1;
#ifdef TRACY_ENABLE
	TracyAlloc(user, size);
#endif
	return user;
}

void* AllocAligned(std::size_t size, std::size_t alignment) noexcept {
#ifdef _WIN32
	return _aligned_malloc(size ? size : 1, alignment);
#else
	if (alignment < sizeof(void*)) {
		alignment = sizeof(void*);
	}
	std::size_t rounded = (((size ? size : 1) + alignment - 1) / alignment) * alignment;
	void* p = nullptr;
	if (posix_memalign(&p, alignment, rounded) != 0) {
		return nullptr;
	}
	return p;
#endif
}

static BlockHeader* HeaderFromUser(void* p) noexcept {
	return static_cast<BlockHeader*>(p) - 1;
}

void Free(void* ptr) noexcept {
	if (!ptr) {
		return;
	}
	BlockHeader* hdr = HeaderFromUser(ptr);
	std::size_t sz = hdr->requestedSize;
#ifdef TRACY_ENABLE
	TracyFree(ptr);
#endif
	g_currentBytes.fetch_sub(sz, std::memory_order_relaxed);
	if (hdr->bucketIndex == kLargeSentinel) {
		g_largeBytes.fetch_sub(sz, std::memory_order_relaxed);
		g_largeCount.fetch_sub(1, std::memory_order_relaxed);
		std::free(hdr);
		return;
	}
	if (hdr->bucketIndex < K_BUCKET_SIZES_SIZE) {
		int idx = (int)hdr->bucketIndex;
		g_bucketInUse[idx].fetch_sub(1, std::memory_order_relaxed);
		hdr->requestedSize = 0;
		PushFreeNode(idx, hdr);
	} else {
		std::free(hdr);
	}
}

void FreeAligned(void* ptr, std::size_t /*alignment*/) noexcept {
	if (!ptr) {
		return;
	}
#ifdef _WIN32
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif
}

void TrimPools() noexcept {
	for (std::size_t i = 0; i < K_BUCKET_SIZES_SIZE; ++i) {
		BlockHeader* node = g_freeHead[i].exchange(nullptr, std::memory_order_acq_rel);
		std::size_t released = 0;
		while (node) {
			auto* hdr = node;
			node = hdr->next;
			released += K_BUCKET_SIZES[i];
			std::free(hdr);
		}
		if (released) {
			g_freeCount[i].store(0, std::memory_order_relaxed);
			// adjust reserved bytes by the total released
			g_poolReservedBytes.fetch_sub(released, std::memory_order_relaxed);
		}
	}
}

Stats GetStats() {
	Stats s {};
	s.currentBytes = g_currentBytes.load(std::memory_order_relaxed);
	s.peakBytes = g_peakBytes.load(std::memory_order_relaxed);
	s.largeCurrentBytes = g_largeBytes.load(std::memory_order_relaxed);
	s.largeAllocCount = g_largeCount.load(std::memory_order_relaxed);
	s.poolReservedBytes = g_poolReservedBytes.load(std::memory_order_relaxed);
	s.buckets.reserve(K_BUCKET_SIZES_SIZE);
	for (std::size_t i = 0; i < K_BUCKET_SIZES_SIZE; ++i) {
		s.buckets.push_back(
		    BucketUsage { K_BUCKET_SIZES[i], g_bucketInUse[i].load(std::memory_order_relaxed), g_freeCount[i].load(std::memory_order_relaxed) }
		);
	}
	return s;
}

void ResetPeak() {
	g_peakBytes.store(g_currentBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

}    // namespace toast::memory

void* operator new(std::size_t s) {
	void* p = toast::memory::Alloc(s);
	if (!p) {
		throw std::bad_alloc();
	}
	return p;
}

void operator delete(void* p) noexcept {
	toast::memory::Free(p);
}

void* operator new[](std::size_t s) {
	void* p = toast::memory::Alloc(s);
	if (!p) {
		throw std::bad_alloc();
	}
	return p;
}

void operator delete[](void* p) noexcept {
	toast::memory::Free(p);
}

void* operator new(std::size_t s, std::align_val_t a) {
	void* p = toast::memory::AllocAligned(s, (std::size_t)a);
	if (!p) {
		throw std::bad_alloc();
	}
	return p;
}

void operator delete(void* p, std::align_val_t a) noexcept {
	toast::memory::FreeAligned(p, (std::size_t)a);
}

void* operator new[](std::size_t s, std::align_val_t a) {
	void* p = toast::memory::AllocAligned(s, (std::size_t)a);
	if (!p) {
		throw std::bad_alloc();
	}
	return p;
}

void operator delete[](void* p, std::align_val_t a) noexcept {
	toast::memory::FreeAligned(p, (std::size_t)a);
}

void operator delete(void* p, std::size_t) noexcept {
	toast::memory::Free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
	toast::memory::Free(p);
}

void* operator new(std::size_t s, const std::nothrow_t&) noexcept {
	return toast::memory::Alloc(s);
}

void* operator new[](std::size_t s, const std::nothrow_t&) noexcept {
	return toast::memory::Alloc(s);
}

void operator delete(void* p, const std::nothrow_t&) noexcept {
	toast::memory::Free(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept {
	toast::memory::Free(p);
}

#else

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>

void* operator new(std::size_t size) {
	void* ptr = std::malloc(size);
	// if (!ptr) { throw std::bad_alloc(); }
	TracyAlloc(ptr, size);
	return ptr;
}

void operator delete(void* ptr) noexcept {
	TracyFree(ptr);
	std::free(ptr);
}

void* operator new[](std::size_t size) {
	void* ptr = std::malloc(size);
	if (!ptr) {
		throw std::bad_alloc();
	}
	TracyAlloc(ptr, size);
	return ptr;
}

void operator delete[](void* ptr) noexcept {
	TracyFree(ptr);
	std::free(ptr);
}

#endif
#endif

// NOLINTEND
