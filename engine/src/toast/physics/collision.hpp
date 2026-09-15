/**
 * @file collision.hpp
 * @author Xein
 * @date 10 Sep 2026
 * @brief Canonical collision-pair records shared by broad and narrow phases
 */

#pragma once

#include "body.hpp"
#include "shape.hpp"

#include <compare>
#include <utility>

namespace physics {

struct BodyShapeKey {
	BodyID body;
	ShapeID shape;
	auto operator<=>(const BodyShapeKey&) const = default;
};

struct BroadPhasePair {
	BodyShapeKey a;
	BodyShapeKey b;
	auto operator<=>(const BroadPhasePair&) const = default;
};

[[nodiscard]]
inline auto canonicalPair(BodyShapeKey first, BodyShapeKey second) -> BroadPhasePair {
	if (second < first) {
		std::swap(first, second);
	}
	return {.a = first, .b = second};
}

}
