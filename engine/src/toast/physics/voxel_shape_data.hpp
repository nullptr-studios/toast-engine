/**
 * @file voxel_shape_data.hpp
 * @author Xein
 * @date 17 Sep 2026
 * @brief Voxel data structures for physics system
 */

#pragma once

#include "anchor_mask.hpp"
#include "component_classification.hpp"
#include "shape.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <toast/voxel/mass_accumulator.hpp>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/voxel/surface.hpp>
#include <toast/voxel/voxel_volume.hpp>

namespace physics {

struct VoxelShapeData {
	voxel::Volume* volume = nullptr;
	voxel::VolumeSurface surface;
	voxel::MassMoments moments;
	voxel::Palette palette;
	voxel::MaterialLibrary materials;
	uint32_t source_revision = 0;
	uint32_t surface_revision = 1;
	uint32_t solid_voxel_count = 0;
	AnchorMask anchor_mask = k_anchor_null;
	bool connectivity_dirty = false;
	std::vector<DetachedComponent> detached_components;
	std::unique_ptr<voxel::Volume> owned_volume;
	BodyID fragment_origin;
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
