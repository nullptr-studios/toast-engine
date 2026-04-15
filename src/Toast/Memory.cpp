#include "Toast/Memory.hpp"

#include <cstdlib>
#include <new>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <atomic>

// Thread-local re-entrancy guard to prevent Tracy from profiling its own allocations
thread_local bool g_inTracyCall = false;

// Global flag to disable Tracy during shutdown to prevent crashes on Linux
std::atomic<bool> g_tracyEnabled = true;

struct TracyGuard {
	bool wasInCall;

	TracyGuard() : wasInCall(g_inTracyCall) {
		g_inTracyCall = true;
	}

	~TracyGuard() {
		g_inTracyCall = wasInCall;
	}
};

#define TRACY_ALLOC_SAFE(ptr, size) \
	do {                              \
		if (g_tracyEnabled && !g_inTracyCall) { \
			TracyGuard _tg;               \
			TracyAlloc(ptr, size);        \
		}                               \
	} while (0)

#define TRACY_FREE_SAFE(ptr) \
	do {                       \
		if (g_tracyEnabled && !g_inTracyCall) { \
			TracyGuard _tg;        \
			TracyFree(ptr);        \
		}                        \
	} while (0)

#else
#define TRACY_ALLOC_SAFE(ptr, size) ((void)0)
#define TRACY_FREE_SAFE(ptr) ((void)0)
#endif

// NOLINTBEGIN

void* operator new(std::size_t size) {
	void* ptr = std::malloc(size ? size : 1);
	if (!ptr) {
		throw std::bad_alloc();
	}
	TRACY_ALLOC_SAFE(ptr, size);
	return ptr;
}

void* operator new[](std::size_t size) {
	void* ptr = std::malloc(size ? size : 1);
	if (!ptr) {
		throw std::bad_alloc();
	}
	TRACY_ALLOC_SAFE(ptr, size);
	return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
	void* ptr = std::malloc(size ? size : 1);
	if (ptr) {
		TRACY_ALLOC_SAFE(ptr, size);
	}
	return ptr;
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
	void* ptr = std::malloc(size ? size : 1);
	if (ptr) {
		TRACY_ALLOC_SAFE(ptr, size);
	}
	return ptr;
}

void* operator new(std::size_t size, std::align_val_t alignment) {
#ifdef _WIN32
	void* ptr = _aligned_malloc(size ? size : 1, static_cast<std::size_t>(alignment));
#else
	std::size_t al = static_cast<std::size_t>(alignment);
	if (al < sizeof(void*)) {
		al = sizeof(void*);
	}
	void* ptr = nullptr;
	if (posix_memalign(&ptr, al, size ? size : 1) != 0) {
		ptr = nullptr;
	}
#endif
	if (!ptr) {
		throw std::bad_alloc();
	}
	TRACY_ALLOC_SAFE(ptr, size);
	return ptr;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
#ifdef _WIN32
	void* ptr = _aligned_malloc(size ? size : 1, static_cast<std::size_t>(alignment));
#else
	std::size_t al = static_cast<std::size_t>(alignment);
	if (al < sizeof(void*)) {
		al = sizeof(void*);
	}
	void* ptr = nullptr;
	if (posix_memalign(&ptr, al, size ? size : 1) != 0) {
		ptr = nullptr;
	}
#endif
	if (!ptr) {
		throw std::bad_alloc();
	}
	TRACY_ALLOC_SAFE(ptr, size);
	return ptr;
}

void operator delete(void* ptr) noexcept {
	TRACY_FREE_SAFE(ptr);
	std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
	TRACY_FREE_SAFE(ptr);
	std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
	TRACY_FREE_SAFE(ptr);
	std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
	TRACY_FREE_SAFE(ptr);
	std::free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
	TRACY_FREE_SAFE(ptr);
	std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
	TRACY_FREE_SAFE(ptr);
	std::free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
	TRACY_FREE_SAFE(ptr);
#ifdef _WIN32
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
	TRACY_FREE_SAFE(ptr);
#ifdef _WIN32
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif
}

// NOLINTEND

#ifdef TRACY_ENABLE
extern "C" {
	/// Disables Tracy profiling during shutdown to prevent crashes
	void DisableTracyProfiling() noexcept {
		g_tracyEnabled = false;
	}
}
#endif

