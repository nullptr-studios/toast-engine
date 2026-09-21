#include "voxel_data_lock.hpp"

namespace physics {

auto voxelDataMutex() -> std::mutex& {
	static std::mutex mutex;
	return mutex;
}

}
