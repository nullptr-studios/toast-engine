/**
 * @file Memory.hpp
 * @author Xein
 * @date 15/03/25
 * @brief Custom memory allocation system with pooling.
 *
 * Provides a bucket-based memory allocator that pools small allocations
 * to reduce fragmentation and improve allocation performance for
 * frequently allocated objects.
 */

#pragma once

#include <cstddef>
#include <vector>

/**
 * @namespace toast::memory
 * @brief Custom memory management utilities.
 *
 * This namespace provides a pooled memory allocator optimized for game engines.
 * Small allocations (up to 4KB) are served from buckets to avoid frequent
 * system allocations. Larger allocations fall back to the system allocator.
 *
 * @par Bucket Sizes:
 * The allocator uses the following bucket sizes: 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes.
 * Allocations are rounded up to the nearest bucket size.
 *
 * @par Thread Safety:
 * All functions are thread-safe and use lock-free algorithms where possible.
 *
 * @par Usage Example:
 * @code
 * // Allocate memory
 * void* ptr = toast::memory::Alloc(100);  // Uses 128-byte bucket
 *
 * // Use the memory...
 *
 * // Free when done
 * toast::memory::Free(ptr);
 *
 * // Check allocation stats
 * auto stats = toast::memory::GetStats();
 * std::cout << "Current: " << stats.currentBytes << " bytes\n";
 * @endcode
 *
 * @note On Windows, the custom allocator is used. On other platforms,
 *       the system allocator may be used directly.
 */
namespace toast::memory {

/**
 * @brief Allocates memory from the pool.
 *
 * Small allocations are served from buckets for better performance.
 * Larger allocations (>4KB) fall back to the system allocator.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or nullptr on allocation failure.
 *
 * @note The actual allocated size may be larger than requested due to bucket rounding.
 * @see Free()
 */
void* Alloc(std::size_t size) noexcept;

/**
 * @brief Allocates memory from the pool, throwing on failure.
 *
 * Same as Alloc() but throws std::bad_alloc instead of returning nullptr.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory.
 * @throws std::bad_alloc if allocation fails.
 *
 * @see Alloc()
 */
void* AllocThrow(std::size_t size);

/**
 * @brief Frees memory previously allocated with Alloc() or AllocThrow().
 *
 * For pooled allocations, the block is returned to the bucket freelist
 * for reuse. For large allocations, memory is returned to the system.
 *
 * @param ptr Pointer to memory to free (nullptr is safely ignored).
 *
 * @warning Passing a pointer not allocated by Alloc/AllocThrow results
 *          in undefined behavior.
 *
 * @see Alloc(), AllocThrow()
 */
void Free(void* ptr) noexcept;

/**
 * @brief Allocates aligned memory.
 *
 * Allocates memory with the specified alignment requirement.
 * Useful for SIMD operations or hardware-specific requirements.
 *
 * @param size Number of bytes to allocate.
 * @param alignment Required alignment (must be power of 2).
 * @return Pointer to aligned memory, or nullptr on failure.
 *
 * @note Must be freed with FreeAligned(), not Free().
 * @see FreeAligned()
 */
void* AllocAligned(std::size_t size, std::size_t alignment) noexcept;

/**
 * @brief Frees aligned memory previously allocated with AllocAligned().
 *
 * @param ptr Pointer to aligned memory to free.
 * @param alignment The alignment used when allocating (for verification).
 *
 * @see AllocAligned()
 */
void FreeAligned(void* ptr, std::size_t alignment) noexcept;

/**
 * @struct BucketUsage
 * @brief Statistics for a single memory bucket.
 */
struct BucketUsage {
	std::size_t blockSize;    ///< Total block size including internal header.
	std::size_t inUse;        ///< Number of blocks currently allocated.
	std::size_t freeCount;    ///< Number of blocks in the freelist (available for reuse).
};

/**
 * @struct Stats
 * @brief Comprehensive memory allocation statistics.
 *
 * Use GetStats() to retrieve current allocation information for
 * debugging, profiling, or memory leak detection.
 */
struct Stats {
	std::size_t currentBytes;            ///< Total user bytes currently allocated.
	std::size_t peakBytes;               ///< Peak user bytes ever allocated.
	std::size_t largeCurrentBytes;       ///< Bytes in large (non-pooled) allocations.
	std::size_t largeAllocCount;         ///< Number of active large allocations.
	std::size_t poolReservedBytes;       ///< Total bytes held by the pool (in-use + free).
	std::vector<BucketUsage> buckets;    ///< Per-bucket allocation statistics.
};

/**
 * @brief Gets current memory allocation statistics.
 * @return Stats structure containing all allocation metrics.
 *
 * @par Example:
 * @code
 * auto stats = toast::memory::GetStats();
 * TOAST_INFO("Memory: {} bytes ({} peak)", stats.currentBytes, stats.peakBytes);
 * for (const auto& bucket : stats.buckets) {
 *     TOAST_TRACE("  Bucket {}: {} in use, {} free", bucket.blockSize, bucket.inUse, bucket.freeCount);
 * }
 * @endcode
 */
Stats GetStats();

/**
 * @brief Resets the peak memory counter.
 *
 * Sets peakBytes to the current allocation level. Useful for
 * per-frame or per-level memory profiling.
 */
void ResetPeak();

/**
 * @brief Releases all free pooled blocks back to the OS.
 *
 * Empties all bucket freelists, returning the memory to the system.
 * Use this when transitioning between levels or when memory pressure
 * is high.
 *
 * @note This does NOT affect currently allocated blocks.
 */
void TrimPools() noexcept;

}    // namespace toast::memory
