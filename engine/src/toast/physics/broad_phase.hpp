/**
 * @file broad_phase.hpp
 * @brief Generates potentially colliding shape pairs
 */

#pragma once

#include "collision.hpp"
#include "collision_world.hpp"

#include <optional>
#include <vector>

namespace physics {

class BroadPhase {
public:
	[[nodiscard]]
	auto findPairs(CollisionWorldView world) const -> std::vector<BroadPhasePair>;

private:
	[[nodiscard]]
	auto testPair(CollisionWorldView world, size_t shape_a, size_t shape_b) const -> std::optional<BroadPhasePair>;
};

}
