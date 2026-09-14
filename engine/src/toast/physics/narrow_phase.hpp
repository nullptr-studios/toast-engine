/**
 * @file narrow_phase.hpp
 * @brief Generates contact manifolds for potentially colliding pairs
 */

#pragma once

#include "collision_world.hpp"
#include "manifold.hpp"

#include <optional>
#include <span>
#include <vector>

namespace physics {

class NarrowPhase {
public:
	[[nodiscard]]
	auto generateManifolds(CollisionWorldView world, std::span<const BroadPhasePair> candidates) const -> std::vector<Manifold>;

private:
	[[nodiscard]]
	auto collide(CollisionWorldView world, BroadPhasePair pair) const -> std::optional<Manifold>;

	[[nodiscard]]
	auto validate(CollisionWorldView world, Manifold& manifold) const -> bool;

	static void flip(Manifold& manifold);
};

}
