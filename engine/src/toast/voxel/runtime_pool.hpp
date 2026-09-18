/**
 * @file runtime_pool.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "brick_pool.hpp"

#include <cstdint>
#include <toast/export.hpp>

namespace voxel {

inline constexpr uint32_t k_runtime_brick_capacity = 1u << 16;

/// @note Main thread only
[[nodiscard]]
TOAST_API auto runtimeBrickPool() -> BrickPool&;

}
