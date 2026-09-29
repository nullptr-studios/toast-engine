/**
 * @file fill_volume.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief A volume that adds voxels to a ProceduralVoxel
 */

#pragma once
#include "voxel_volume.hpp"

namespace toast {

/** Adds voxels to the shape */
class [[ToastNode]] TOAST_API FillVolume : public VoxelVolume { };

}
