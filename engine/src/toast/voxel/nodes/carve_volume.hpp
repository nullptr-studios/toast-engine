/**
 * @file carve_volume.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief A volume that empties a ProceduralVoxel
 */

#pragma once
#include "voxel_volume.hpp"

namespace toast {

/** Empties the shape wherever it has voxels */
class [[ToastNode, Icon("BoxOccluder")]] TOAST_API CarveVolume : public VoxelVolume {
public:
	[[nodiscard]]
	auto kind() const noexcept -> PieceKind override {
		return PieceKind::carve;
	}
};

}
