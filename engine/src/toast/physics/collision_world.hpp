/**
 * @file collision_world.hpp
 * @brief Read-only access to physics data used by collision detection
 */

#pragma once

#include "body.hpp"
#include "shape.hpp"
#include "voxel_shape_data.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace physics {

[[nodiscard]]
constexpr auto bodyPairKey(BodyID a, BodyID b) -> uint64_t {
	const uint32_t low = std::min(a.slot, b.slot);
	const uint32_t high = std::max(a.slot, b.slot);
	return (static_cast<uint64_t>(low) << 32) | high;
}

struct CollisionWorldView {
	std::span<const BodySlot> bodies;
	std::span<const ShapeSlot> shapes;
	std::span<const VoxelShapeSlot> voxel_shapes;
	std::span<const uint64_t> excluded_pairs;

	[[nodiscard]]
	auto pairExcluded(BodyID a, BodyID b) const -> bool {
		return not excluded_pairs.empty() && std::binary_search(excluded_pairs.begin(), excluded_pairs.end(), bodyPairKey(a, b));
	}

	[[nodiscard]]
	auto body(BodyID id) const -> const Body* {
		if (id.slot >= bodies.size()) {
			return nullptr;
		}

		const BodySlot& slot = bodies[id.slot];
		return slot.occupied && slot.generation == id.generation ? &slot.body : nullptr;
	}

	[[nodiscard]]
	auto shape(ShapeID id) const -> const Shape* {
		if (id.slot >= shapes.size()) {
			return nullptr;
		}

		const ShapeSlot& slot = shapes[id.slot];
		return slot.occupied && slot.generation == id.generation ? &slot.shape : nullptr;
	}

	[[nodiscard]]
	auto voxelData(VoxelDataID id) const -> const VoxelShapeData* {
		if (id.slot >= voxel_shapes.size()) {
			return nullptr;
		}

		const VoxelShapeSlot& slot = voxel_shapes[id.slot];
		return slot.generation == id.generation && slot.data.has_value() ? &slot.data.value() : nullptr;
	}
};

}
