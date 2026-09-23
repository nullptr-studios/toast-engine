#include "runtime_pool.hpp"

namespace voxel {

auto runtimeBrickPool() -> BrickPool& {
	// Leaked since volumes free bricks in destructors that can run during static teardown
	static BrickPool* pool = new BrickPool(k_runtime_brick_capacity);
	return *pool;
}

auto runtimePoolMutex() -> std::mutex& {
	// Leaked like the pool
	static std::mutex* mutex = new std::mutex;
	return *mutex;
}

}
