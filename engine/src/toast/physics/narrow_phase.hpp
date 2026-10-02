/**
 * @file narrow_phase.hpp
 * @brief Generates contact manifolds for potentially colliding pairs
 */

#pragma once

#include "collision_world.hpp"
#include "manifold.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace physics {

enum class NarrowPhasePairType : uint8_t {
	sphere_sphere,
	sphere_box,
	sphere_capsule,
	box_box,
	box_capsule,
	capsule_capsule,
	sphere_voxel,
	box_voxel,
	capsule_voxel,
	voxel_voxel,
	count
};

struct ManifoldQueue {
	std::vector<Manifold> manifolds;
	std::array<size_t, static_cast<size_t>(NarrowPhasePairType::count)> pair_candidates = {};
	std::array<double, static_cast<size_t>(NarrowPhasePairType::count)> pair_time_ms = {};
	size_t candidate_count = 0;
	size_t collision_count = 0;
	size_t rejected_manifold_count = 0;
	size_t contact_count = 0;
};

class NarrowPhase {
public:
	[[nodiscard]]
	auto generateManifolds(CollisionWorldView world, std::span<const BroadPhasePair> candidates) const -> ManifoldQueue;

	/// Same validate/count/keep-or-reject step generateManifolds runs so a split pair can fold results in too
	void accumulate(std::span<Manifold> manifolds, CollisionWorldView world, ManifoldQueue& queue) const;

private:
	void collide(CollisionWorldView world, BroadPhasePair pair, std::vector<Manifold>& output) const;

	[[nodiscard]]
	auto validate(CollisionWorldView world, Manifold& manifold) const -> bool;

	static void flip(Manifold& manifold);
};

}
