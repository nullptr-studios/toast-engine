/**
 * @file procedural_pool.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief Bricks for the grids ProceduralVoxel pieces draw into
 */

#pragma once
#include "brick_pool.hpp"

#include <cstdint>
#include <toast/export.hpp>

namespace voxel {

/// @note Main thread only like the runtime pool
[[nodiscard]]
TOAST_API auto proceduralBrickPool() -> BrickPool&;

}
