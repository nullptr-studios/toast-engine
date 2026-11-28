#include "voxel_params.hpp"

namespace voxel {

auto voxelParams() -> VoxelParams& {
	static VoxelParams instance;
	return instance;
}

}
