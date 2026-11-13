/// @file gi_clip_math.hpp
/// @author dario
/// @date 10/11/2026

#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <span>

namespace renderer::gi_clip {

struct Box {
	glm::ivec3 min {0};
	/// Exclusive
	glm::ivec3 max {0};
};

[[nodiscard]]
inline auto empty(const Box& box) -> bool {
	return glm::any(glm::greaterThanEqual(box.min, box.max));
}

[[nodiscard]]
inline auto origin(glm::vec3 camera, float cell, int resolution) -> glm::ivec3 {
	return 2 * glm::ivec3(glm::floor(camera / (2.0f * cell))) - glm::ivec3(resolution / 2);
}

[[nodiscard]]
inline auto wrap(glm::ivec3 cell, int resolution) -> glm::ivec3 {
	return ((cell % resolution) + resolution) % resolution;
}

/// @return count written to @p out
inline auto scrolled(glm::ivec3 previous, glm::ivec3 current, int resolution, std::span<Box, 3> out) -> uint32_t {
	const glm::ivec3 delta = current - previous;
	if (glm::any(glm::greaterThanEqual(glm::abs(delta), glm::ivec3(resolution)))) {
		out[0] = {.min = current, .max = current + resolution};
		return 1;
	}
	Box rest {.min = current, .max = current + resolution};
	uint32_t count = 0;
	for (int axis = 0; axis < 3; ++axis) {
		if (delta[axis] == 0) {
			continue;
		}
		Box slab = rest;
		if (delta[axis] > 0) {
			slab.min[axis] = previous[axis] + resolution;
			rest.max[axis] = slab.min[axis];
		} else {
			slab.max[axis] = previous[axis];
			rest.min[axis] = slab.max[axis];
		}
		out[count++] = slab;
	}
	return count;
}

[[nodiscard]]
inline auto cells(glm::vec3 low, glm::vec3 high, float cell, glm::ivec3 origin, int resolution) -> Box {
	return {
	  .min = glm::max(glm::ivec3(glm::floor(low / cell)), origin),
	  .max = glm::min(glm::ivec3(glm::floor(high / cell)) + 1, origin + resolution),
	};
}

}
