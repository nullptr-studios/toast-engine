#include "voxel_data_lock.hpp"

#include <toast/voxel/runtime_pool.hpp>

namespace physics {

auto voxelDataMutex() -> TOAST_LOCKABLE_BASE(std::recursive_mutex) & {
	return voxel::runtimePoolMutex();
}

}
