#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <atomic>
#include <cassert>
#include <glm/glm.hpp>
#include <thread>
#include <toast/physics/raycast.hpp>
#include <toast/physics/nodes/rigidbody.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/voxel/brick_pool.hpp>
#include <toast/voxel/voxel_volume.hpp>

using namespace toast;
using WorldTestAccess = toast::_detail::WorldTestAccess;

TOAST_TEST_NAMED("physics", "physics/02-query_snapshot_concurrent", test_physics_02_query_snapshot_concurrent) {
	WorldTestAccess::initThreadPool();
	auto world = WorldTestAccess::createWorld();
	physics::Simulator simulator;
	physics::Simulator::bindToThisThread();
	auto body = WorldTestAccess::createTypedNode(*world, "physics::StaticRigidbody", "body");
	auto collider = WorldTestAccess::createTypedNode(*world, "physics::BoxCollider", "box");
	WorldTestAccess::attachChild(*body, *collider);
	auto rigidbody = body.as<physics::Rigidbody>();
	physics::Simulator::registerRigidbody(*rigidbody);
	physics::Simulator::callTick();
	const auto hit = physics::Simulator::raycast({-2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 5.0f);
	assert(hit.has_value());
	assert(hit->distance > 1.4f && hit->distance < 1.6f);
	assert(physics::sphereOverlap({}, 0.2f).has_value());
	assert(physics::overlapAABB(glm::vec3(-0.1f), glm::vec3(0.1f)));

	std::atomic<bool> running {true};
	std::thread reader([&] {
		while (running.load(std::memory_order_relaxed)) {
			(void)physics::Simulator::raycast({-2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 5.0f);
			(void)physics::sphereOverlap({}, 0.2f);
			(void)physics::overlapAABB(glm::vec3(-0.1f), glm::vec3(0.1f));
		}
	});
	for (int i = 0; i < 32; ++i) {
		physics::Simulator::unregisterRigidbody(*rigidbody);
		if (i == 0) {
			assert(physics::Simulator::raycast({-2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 5.0f));
		}
		physics::Simulator::callTick();
		if (i == 0) {
			assert(!physics::Simulator::raycast({-2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 5.0f));
		}
		physics::Simulator::registerRigidbody(*rigidbody);
		physics::Simulator::callTick();
	}
	running.store(false, std::memory_order_relaxed);
	reader.join();
}

TOAST_TEST_NAMED("physics", "physics/03-voxel_snapshot_cow", test_physics_03_voxel_snapshot_cow) {
	voxel::BrickPool pool(8);
	{
		voxel::Volume live(pool, {1, 1, 1});
		assert(live.setVoxel({0, 0, 0}, 3).changed);
		voxel::Volume snapshot = voxel::Volume::instanceOf(live);
		assert(pool.allocatedCount() == 1);
		assert(live.setVoxel({0, 0, 0}, 4).changed);
		assert(snapshot.materialAt({0, 0, 0}) == 3);
		assert(live.materialAt({0, 0, 0}) == 4);
		assert(pool.allocatedCount() == 2);
		live.setBrickUniform({0, 0, 0}, 5);
		assert(snapshot.materialAt({0, 0, 0}) == 3);
		assert(pool.allocatedCount() == 1);
	}
	assert(pool.allocatedCount() == 0);
}
