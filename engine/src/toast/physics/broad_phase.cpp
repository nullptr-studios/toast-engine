#include "broad_phase.hpp"

#include <tracy/Tracy.hpp>

namespace physics {

auto BroadPhase::findPairs(CollisionWorldView world) const -> std::vector<BroadPhasePair> {
	ZoneScoped;

	std::vector<BroadPhasePair> pairs;

	for (size_t i = 0; i < world.shapes.size(); ++i) {
		if (not world.shapes[i].occupied || not world.shapes[i].shape.enabled) {
			continue;
		}

		for (size_t j = i + 1; j < world.shapes.size(); ++j) {
			if (auto pair = testPair(world, i, j)) {
				pairs.emplace_back(*pair);
			}
		}
	}

	ZoneValue(static_cast<uint64_t>(pairs.size()));
	return pairs;
}

auto BroadPhase::testPair(CollisionWorldView world, size_t shape_a_index, size_t shape_b_index) const
    -> std::optional<BroadPhasePair> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(shape_a_index) << 32) | static_cast<uint64_t>(shape_b_index));

	const ShapeSlot& shape_a = world.shapes[shape_a_index];
	const ShapeSlot& shape_b = world.shapes[shape_b_index];
	if (not shape_a.occupied || not shape_b.occupied || not shape_a.shape.enabled || not shape_b.shape.enabled) {
		return std::nullopt;
	}

	if (shape_a.shape.owner == shape_b.shape.owner) {
		return std::nullopt;
	}

	const Body* body_a = world.body(shape_a.shape.owner);
	const Body* body_b = world.body(shape_b.shape.owner);
	if (not body_a || not body_b || not body_a->enabled || not body_b->enabled) {
		return std::nullopt;
	}

	if (body_a->inverse_mass == 0.0f && body_b->inverse_mass == 0.0f) {
		return std::nullopt;
	}

	return canonicalPair(
	    BodyShapeKey {
	      .body = shape_a.shape.owner,
	      .shape = {.slot = static_cast<uint32_t>(shape_a_index), .generation = shape_a.generation},
  },
	    BodyShapeKey {
	      .body = shape_b.shape.owner,
	      .shape = {.slot = static_cast<uint32_t>(shape_b_index), .generation = shape_b.generation},
	    }
	);
}

}
