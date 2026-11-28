#include "procedural_pool.hpp"

#include "voxel_params.hpp"

namespace voxel {

auto proceduralBrickPool() -> BrickPool& {
	static BrickPool* pool = new BrickPool(static_cast<uint32_t>(voxelParams().procedural_bricks));
	return *pool;
}

}
