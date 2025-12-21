#pragma once

#include <cstddef>
#include <vector>

namespace toast::memory {

void* Alloc(std::size_t size) noexcept;                                  // returns nullptr on OOM
void* AllocThrow(std::size_t size);                                      // throws std::bad_alloc on OOM
void Free(void* ptr) noexcept;                                           // matches Alloc/AllocThrow

void* AllocAligned(std::size_t size, std::size_t alignment) noexcept;    // simple aligned alloc wrapper
void FreeAligned(void* ptr, std::size_t alignment) noexcept;             // frees aligned block

struct BucketUsage {
	std::size_t blockSize;    // total block size including header
	std::size_t inUse;        // number of blocks currently checked out
	std::size_t freeCount;    // number of reusable blocks in the freelist
};

struct Stats {
	std::size_t currentBytes;            // total user bytes currently allocated
	std::size_t peakBytes;               // peak user bytes observed
	std::size_t largeCurrentBytes;       // bytes in large allocations (not pooled)
	std::size_t largeAllocCount;         // number of outstanding large allocations
	std::size_t poolReservedBytes;       // raw bytes owned by pooled blocks (inUse + free)
	std::vector<BucketUsage> buckets;    // per-bucket usage
};

Stats GetStats();
void ResetPeak();

// Pooling
void TrimPools() noexcept;    // release all free pooled blocks back to OS

}    // namespace toast::memory
