/**
 * @file runtime_pool.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "brick_pool.hpp"

#include <cstdint>
#include <mutex>
#include <toast/export.hpp>
#include <toast/lock_profiling.hpp>

namespace voxel {

/// @note Main thread only
[[nodiscard]]
TOAST_API auto runtimeBrickPool() -> BrickPool&;

/// Held while physics writes runtime volumes and while the renderer packs them
[[nodiscard]]
TOAST_API auto runtimePoolMutex() -> TOAST_LOCKABLE_BASE(std::recursive_mutex) &;

}
