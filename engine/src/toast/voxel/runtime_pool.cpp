#include "runtime_pool.hpp"

namespace voxel {

auto runtimeBrickPool() -> BrickPool& {
	// Leaked since volumes free bricks in destructors that can run during static teardown
	static BrickPool* pool = new BrickPool(k_runtime_brick_capacity);
	return *pool;
}

auto runtimePoolMutex() -> TOAST_LOCKABLE_BASE(std::recursive_mutex) & {
	struct Holder {
		TOAST_LOCKABLE(std::recursive_mutex, mutex, "Voxel runtime pool");
	};

	// Leaked like the pool
	static auto* holder = new Holder;
	return holder->mutex;
}

}
