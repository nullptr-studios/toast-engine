/**
 * @file voxel_shape_data.hpp
 * @author Xein
 * @date 17 Sep 2026
 * @brief Voxel data structures for physics system
 */

#pragma once

#include "shape.hpp"

#include <cstdint>
#include <optional>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/voxel/surface.hpp>
#include <toast/voxel/voxel_volume.hpp>

namespace physics {

struct VoxelShapeData {
	voxel::Volume volume;
	voxel::VolumeSurface surface;
	voxel::Palette palette;
	voxel::MaterialLibrary materials;
	uint32_t surface_revision = 1;
};

struct VoxelShapeSlot {
	VoxelShapeSlot() = default;
	VoxelShapeSlot(const VoxelShapeSlot&) = delete;
	auto operator=(const VoxelShapeSlot&) -> VoxelShapeSlot& = delete;
	VoxelShapeSlot(VoxelShapeSlot&&) noexcept = default;
	auto operator=(VoxelShapeSlot&&) noexcept -> VoxelShapeSlot& = default;

	std::optional<VoxelShapeData> data;
	uint32_t generation = 1;
};

}
