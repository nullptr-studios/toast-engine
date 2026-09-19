#include "component_classification.hpp"

#include <tracy/Tracy.hpp>

namespace physics {

namespace {

auto touchesFace(const voxel::BrickOccupancy& voxels, AnchorFace face) -> bool {
	ZoneScoped;

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
	std::vector<ComponentClass> classes(c.component_count, ComponentClass::dropped);

	for (const voxel::BrickPiece& piece : c.pieces) {
		if (classes[piece.component] == ComponentClass::anchored) {
			continue;
		}
		if (pieceTouchesAnchor(piece, brick_size, mask)) {
			classes[piece.component] = ComponentClass::anchored;
		}
	}

	for (uint32_t component = 0; component < c.component_count; ++component) {
		if (classes[component] == ComponentClass::anchored) {
			continue;
		}
		if (c.voxelCount(component) >= k_min_fragment_voxels) {
			classes[component] = ComponentClass::detached;
		}
	}

	return classes;
}

auto buildDetachedComponents(const voxel::Connectivity& c, std::span<const ComponentClass> classes, glm::uvec3 brick_size)
    -> std::vector<DetachedComponent> {
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

}
