/**
 * @file voxel_render.hpp
 * @author Xein
 * @date 18 Sep 2026
 * @brief Read-only voxel render records
 */

#pragma once

#include "shape.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/voxel_volume.hpp>

namespace physics {

struct VoxelRenderRecord {
	ShapeID shape;
	const voxel::Volume* volume = nullptr;
	const voxel::Palette* palette = nullptr;
	glm::mat4 transform {1.0f};
	uint32_t revision = 0;
};

}
