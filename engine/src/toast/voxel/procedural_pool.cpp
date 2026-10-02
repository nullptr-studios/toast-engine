#include "procedural_pool.hpp"

namespace voxel {

auto proceduralBrickPool() -> BrickPool& {
	static BrickPool* pool = new BrickPool(k_procedural_brick_capacity);
	return *pool;
}

}
