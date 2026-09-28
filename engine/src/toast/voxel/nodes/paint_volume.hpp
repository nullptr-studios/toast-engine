/**
 * @file paint_volume.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief A volume that recolors a ProceduralVoxel
 */

#pragma once
#include "voxel_volume.hpp"

namespace toast {

/** Recolors the shape where it is already solid */
class [[ToastNode, Icon("Blend")]] TOAST_API PaintVolume : public VoxelVolume {
public:
	[[nodiscard]]
	auto kind() const noexcept -> PieceKind override {
		return PieceKind::paint;
	}

	[[nodiscard]]
	auto writeMode() const noexcept -> voxel::WriteMode override {
		return voxel::WriteMode::solid_only;
	}
};

}
