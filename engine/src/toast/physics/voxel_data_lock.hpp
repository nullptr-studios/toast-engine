/**
 * @file voxel_data_lock.hpp
 * @author Xein
 * @date 18 Sep 2026
 * @brief TODO:
 */

#pragma once

#include <mutex>
#include <toast/export.hpp>
#include <toast/lock_profiling.hpp>

namespace physics {

[[nodiscard]]
TOAST_API auto voxelDataMutex() -> TOAST_LOCKABLE_BASE(std::recursive_mutex) &;

}
