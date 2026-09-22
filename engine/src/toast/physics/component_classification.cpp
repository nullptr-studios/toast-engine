#include "component_classification.hpp"

#include <algorithm>
#include <limits>
#include <tracy/Tracy.hpp>
#include <tuple>

namespace physics {

namespace {

auto touchesFace(const voxel::BrickOccupancy& voxels, AnchorFace face) -> bool {
	switch (face) {
		case AnchorFace::neg_x: {
			for (auto s : voxels.slices) {
				if ((s & voxel::k_column_x_min) != 0) {
					return true;
				}
			}
			return false;
		}
		case AnchorFace::pos_x: {
			for (auto s : voxels.slices) {
				if ((s & voxel::k_column_x_max) != 0) {
					return true;
				}
			}
			return false;
		}
		case AnchorFace::neg_y: {
			for (auto s : voxels.slices) {
				if ((s & voxel::k_row_y_min) != 0) {
					return true;
				}
			}
			return false;
		}
		case AnchorFace::pos_y: {
			for (auto s : voxels.slices) {
				if ((s & voxel::k_row_y_max) != 0) {
					return true;
				}
			}
			return false;
		}
		case AnchorFace::neg_z: {
			return voxels[0] != 0;
		}
		case AnchorFace::pos_z: {
			return voxels[voxel::k_brick_dim - 1] != 0;
		}
	}
	return false;
}

auto pieceTouchesAnchor(const voxel::BrickPiece& piece, glm::uvec3 brick_dims, AnchorMask mask) -> bool {
	const auto max_x = static_cast<int32_t>(brick_dims.x) - 1;
	const auto max_y = static_cast<int32_t>(brick_dims.y) - 1;
	const auto max_z = static_cast<int32_t>(brick_dims.z) - 1;

	if (hasAnchorFace(mask, AnchorFace::neg_x) && piece.brick.x == 0 && touchesFace(piece.voxels, AnchorFace::neg_x)) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::pos_x) && piece.brick.x == max_x && touchesFace(piece.voxels, AnchorFace::pos_x)) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::neg_y) && piece.brick.y == 0 && touchesFace(piece.voxels, AnchorFace::neg_y)) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::pos_y) && piece.brick.y == max_y && touchesFace(piece.voxels, AnchorFace::pos_y)) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::neg_z) && piece.brick.z == 0 && touchesFace(piece.voxels, AnchorFace::neg_z)) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::pos_z) && piece.brick.z == max_z && touchesFace(piece.voxels, AnchorFace::pos_z)) {
		return true;
	}
	return false;
}

auto brickSlot(glm::ivec3 brick, glm::uvec3 brick_dims) -> uint32_t {
	// clang-format off
	return static_cast<uint32_t>(brick.x) +
	       (static_cast<uint32_t>(brick.y) * brick_dims.x) +
	       (static_cast<uint32_t>(brick.z) * brick_dims.x * brick_dims.y);
	// clang-format on
}

}

auto classifyComponents(const voxel::Connectivity& c, glm::uvec3 brick_size, AnchorMask mask) -> std::vector<ComponentClass> {
	ZoneScopedN("physics::ClassifyComponents");
	std::vector<ComponentClass> classes(c.component_count, ComponentClass::dropped);

	for (const voxel::BrickPiece& piece : c.pieces) {
		if (classes[piece.component] == ComponentClass::anchored) {
			continue;
		}
		if (pieceTouchesAnchor(piece, brick_size, mask)) {
			classes[piece.component] = ComponentClass::anchored;
		}
	}

	// size is queuePendingFragments job now, not a dead zone that never spawns or clears
	for (uint32_t component = 0; component < c.component_count; ++component) {
		if (classes[component] == ComponentClass::anchored) {
			continue;
		}
		classes[component] = ComponentClass::detached;
	}

	return classes;
}

auto buildDetachedComponents(const voxel::Connectivity& c, std::span<const ComponentClass> classes, glm::uvec3 brick_size)
    -> std::vector<DetachedComponent> {
	ZoneScopedN("physics::BuildDetachedComponents");
	std::vector<DetachedComponent> components(classes.size());
	for (size_t i = 0; i < classes.size(); ++i) {
		components[i].component = static_cast<uint32_t>(i);
	}

	for (const voxel::BrickPiece& piece : c.pieces) {
		if (classes[piece.component] != ComponentClass::detached) {
			continue;
		}
		DetachedComponent& out = components[piece.component];
		out.voxel_count += voxel::popCount(piece.voxels);
		out.pieces.push_back(piece);
	}

	std::erase_if(components, [](const DetachedComponent& c) { return c.pieces.empty(); });

	std::ranges::sort(components, {}, [brick_size](const DetachedComponent& c) {
		uint32_t min_slot = std::numeric_limits<uint32_t>::max();
		for (const voxel::BrickPiece& piece : c.pieces) {
			min_slot = std::min(min_slot, brickSlot(piece.brick, brick_size));
		}
		return min_slot;
	});

	return components;
}

namespace {

struct RunningCluster {
	glm::ivec3 min {std::numeric_limits<int32_t>::max()};
	glm::ivec3 max {std::numeric_limits<int32_t>::min()};
	DetachedComponent component;
};

[[nodiscard]]
auto fitsWithinCap(glm::ivec3 min, glm::ivec3 max, int32_t cap) -> bool {
	const glm::ivec3 extent = max - min + glm::ivec3(1);
	return extent.x <= cap && extent.y <= cap && extent.z <= cap;
}

}

auto splitBySpatialCompactness(std::vector<DetachedComponent> components, int32_t max_extent_bricks)
    -> std::vector<DetachedComponent> {
	ZoneScoped;

	std::vector<DetachedComponent> result;
	result.reserve(components.size());

	for (DetachedComponent& component : components) {
		if (component.pieces.empty()) {
			continue;
		}

		glm::ivec3 min {std::numeric_limits<int32_t>::max()};
		glm::ivec3 max {std::numeric_limits<int32_t>::min()};
		for (const voxel::BrickPiece& piece : component.pieces) {
			min = glm::min(min, piece.brick);
			max = glm::max(max, piece.brick);
		}

		if (fitsWithinCap(min, max, max_extent_bricks)) {
			result.push_back(std::move(component));
			continue;
		}

		std::ranges::sort(component.pieces, {}, [](const voxel::BrickPiece& piece) {
			return std::tuple(piece.brick.z, piece.brick.y, piece.brick.x);
		});

		std::vector<RunningCluster> clusters;
		for (const voxel::BrickPiece& piece : component.pieces) {
			RunningCluster* target = nullptr;
			for (RunningCluster& cluster : clusters) {
				if (fitsWithinCap(glm::min(cluster.min, piece.brick), glm::max(cluster.max, piece.brick), max_extent_bricks)) {
					target = &cluster;
					break;
				}
			}

			if (target == nullptr) {
				clusters.push_back(RunningCluster {.min = piece.brick, .max = piece.brick});
				target = &clusters.back();
				target->component.component = component.component;
			}

			target->min = glm::min(target->min, piece.brick);
			target->max = glm::max(target->max, piece.brick);
			target->component.voxel_count += voxel::popCount(piece.voxels);
			target->component.pieces.push_back(piece);
		}

		for (RunningCluster& cluster : clusters) {
			result.push_back(std::move(cluster.component));
		}
	}

	return result;
}

}
