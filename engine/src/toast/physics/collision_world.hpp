/**
 * @file collision_world.hpp
 * @brief Read-only access to physics data used by collision detection
 */

#pragma once

#include "body.hpp"
#include "shape.hpp"

#include <span>

namespace physics {

struct CollisionWorldView {
	std::span<const BodySlot> bodies;
	std::span<const ShapeSlot> shapes;

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
};

}
