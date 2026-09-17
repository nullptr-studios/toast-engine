/**
 * @file collision_world.hpp
 * @brief Read-only access to physics data used by collision detection
 */

#pragma once

#include "body.hpp"
#include "shape.hpp"
#include "voxel_shape_data.hpp"

#include <span>

namespace physics {

struct CollisionWorldView {
	std::span<const BodySlot> bodies;
	std::span<const ShapeSlot> shapes;
	std::span<const VoxelShapeSlot> voxel_shapes;

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
