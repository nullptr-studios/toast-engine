#include "component_classification.hpp"

#include <algorithm>
#include <limits>
#include <tracy/Tracy.hpp>
#include <tuple>

namespace physics {

namespace {

/// Lowest and highest occupied voxel per axis
struct OccupiedExtent {
	glm::ivec3 min {std::numeric_limits<int32_t>::max()};
	glm::ivec3 max {std::numeric_limits<int32_t>::min()};
};

auto extentOf(const voxel::BrickPiece& piece) -> OccupiedExtent {
	constexpr auto dim = static_cast<int32_t>(voxel::k_brick_dim);
	OccupiedExtent out;
	uint64_t all = 0;
	for (int32_t z = 0; z < dim; ++z) {
		const uint64_t slice = piece.voxels[static_cast<uint32_t>(z)];
		if (slice == 0) {
			continue;
		}
		all |= slice;
		out.min.z = std::min(out.min.z, (piece.brick.z * dim) + z);
		out.max.z = std::max(out.max.z, (piece.brick.z * dim) + z);
	}
	for (int32_t i = 0; i < dim; ++i) {
		if ((all & (voxel::k_column_x_min << i)) != 0) {
			out.min.x = std::min(out.min.x, (piece.brick.x * dim) + i);
			out.max.x = std::max(out.max.x, (piece.brick.x * dim) + i);
		}
		if ((all & (voxel::k_row_y_min << (i * dim))) != 0) {
			out.min.y = std::min(out.min.y, (piece.brick.y * dim) + i);
			out.max.y = std::max(out.max.y, (piece.brick.y * dim) + i);
		}
	}
	return out;
}

/// Anchors are the outermost occupied voxels, not the edge of the volume, since a shape rarely fills its bricks exactly
auto pieceTouchesAnchor(const voxel::BrickPiece& piece, const OccupiedExtent& shape, AnchorMask mask) -> bool {
	const OccupiedExtent mine = extentOf(piece);
	if (hasAnchorFace(mask, AnchorFace::neg_x) && mine.min.x == shape.min.x) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::pos_x) && mine.max.x == shape.max.x) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::neg_y) && mine.min.y == shape.min.y) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::pos_y) && mine.max.y == shape.max.y) {
		return true;
	}
	if (hasAnchorFace(mask, AnchorFace::neg_z) && mine.min.z == shape.min.z) {
		return true;
	}
	return hasAnchorFace(mask, AnchorFace::pos_z) && mine.max.z == shape.max.z;
}

auto brickSlot(glm::ivec3 brick, glm::uvec3 brick_dims) -> uint32_t {
	// clang-format off
	return static_cast<uint32_t>(brick.x) +
	       (static_cast<uint32_t>(brick.y) * brick_dims.x) +
	       (static_cast<uint32_t>(brick.z) * brick_dims.x * brick_dims.y);
	// clang-format on
}

}

auto classifyComponents(const voxel::Connectivity& c, glm::uvec3 /*brick_size*/, AnchorMask mask) -> std::vector<ComponentClass> {
	ZoneScopedN("physics::ClassifyComponents");
	std::vector<ComponentClass> classes(c.component_count, ComponentClass::dropped);

	OccupiedExtent shape;
	for (const voxel::BrickPiece& piece : c.pieces) {
		const OccupiedExtent extent = extentOf(piece);
		shape.min = glm::min(shape.min, extent.min);
		shape.max = glm::max(shape.max, extent.max);
	}

	for (const voxel::BrickPiece& piece : c.pieces) {
		if (classes[piece.component] == ComponentClass::anchored) {
			continue;
		}
		if (pieceTouchesAnchor(piece, shape, mask)) {
			classes[piece.component] = ComponentClass::anchored;
		}
	}

	// Size filtering happens when fragments are queued so discarded debris is also cleared from the source.
	for (uint32_t component = 0; component < c.component_count; ++component) {
		if (classes[component] == ComponentClass::anchored) {
			continue;
		}
		classes[component] = ComponentClass::detached;
	}

	return classes;
}

auto buildDetachedComponents(
    const voxel::Connectivity& c, std::span<const ComponentClass> classes, glm::uvec3 brick_size, ComponentClass target_class
) -> std::vector<DetachedComponent> {
	ZoneScopedN("physics::BuildDetachedComponents");
	std::vector<DetachedComponent> components(classes.size());
	for (size_t i = 0; i < classes.size(); ++i) {
		components[i].component = static_cast<uint32_t>(i);
	}

	for (const voxel::BrickPiece& piece : c.pieces) {
		if (classes[piece.component] != target_class) {
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
