#include "voxel_data_lock.hpp"

#include <toast/voxel/runtime_pool.hpp>

namespace physics {

auto voxelDataMutex() -> std::recursive_mutex& {
	return voxel::runtimePoolMutex();
}

}
