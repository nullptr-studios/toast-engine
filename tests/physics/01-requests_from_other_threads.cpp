#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <glm/glm.hpp>
#include <toast/physics/aabb.hpp>
#include <toast/physics/nodes/dynamic_rigidbody.hpp>
#include <toast/physics/nodes/rigidbody.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/thread_pool.hpp>

using namespace toast;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

/// A rigidbody of the given type with a one metre box collider under it, at the origin
struct Body {
	Box<Node> node;
	Box<Node> collider;

	explicit Body(World& world, const char* type = "physics::StaticRigidbody") {
		node = WorldTestAccess::createTypedNode(world, type, "body");
		collider = WorldTestAccess::createTypedNode(world, "physics::BoxCollider", "box");
		WorldTestAccess::attachChild(*node, *collider);
	}

	auto rigidbody() -> physics::Rigidbody& { return *node.as<physics::Rigidbody>(); }
};

/// Whether the simulator has a shape at the origin
auto occupied(const physics::Simulator& simulator) -> bool {
	return simulator.overlapAABB(physics::AABB {.min = glm::vec3(-0.1f), .max = glm::vec3(0.1f)});
}

/// Runs `work` on a pool worker and waits for it
template<typename Work>
void onWorker(Work&& work) {
	ThreadPool::push(std::forward<Work>(work)).get();
}

}

// The physics simulator belongs to the thread that ticks the engine. A script running on a worker that creates a node with a
// body, or enables or removes one, used to change the simulator from that worker, in the middle of a step or beside the
// thread that owns it. The request is kept for the simulator thread now, which carries it out in order at the start of its
// next tick
TOAST_TEST_NAMED("physics", "physics/01-requests_from_other_threads", test_physics_01_requests_from_other_threads) {
	WorldTestAccess::initThreadPool();
	auto world = WorldTestAccess::createWorld();
	physics::Simulator simulator;
	physics::Simulator::bindToThisThread();
	assert(physics::Simulator::onSimulatorThread());

	Body body(*world);
	physics::Rigidbody& rigidbody = body.rigidbody();

	// A worker registers the body: nothing happens until the simulator thread gets to it
	onWorker([&] {
		assert(!physics::Simulator::onSimulatorThread());
		physics::Simulator::registerRigidbody(rigidbody);
	});
	assert(physics::Simulator::pendingRequests() == 1);
	assert(!occupied(simulator));

	physics::Simulator::callTick();
	assert(physics::Simulator::pendingRequests() == 0);
	assert(physics::Simulator::stepProfile().body_count == 1);
	assert(occupied(simulator));

	// Asking to remove it is kept too, and nothing is lost when the requests that follow each other are for the same body
	onWorker([&] {
		physics::Simulator::unregisterRigidbody(rigidbody);
		physics::Simulator::registerRigidbody(rigidbody);
		physics::Simulator::unregisterRigidbody(rigidbody);
	});
	assert(physics::Simulator::pendingRequests() == 3);
	assert(occupied(simulator));

	physics::Simulator::callTick();
	assert(physics::Simulator::pendingRequests() == 0);
	assert(physics::Simulator::stepProfile().body_count == 0);
	assert(!occupied(simulator));

	// The simulator thread itself is never made to wait: its own calls run at once
	physics::Simulator::registerRigidbody(rigidbody);
	assert(physics::Simulator::pendingRequests() == 0);
	physics::Simulator::callTick();
	assert(physics::Simulator::stepProfile().body_count == 1);
	physics::Simulator::unregisterRigidbody(rigidbody);
	assert(physics::Simulator::pendingRequests() == 0);
	physics::Simulator::callTick();
	assert(physics::Simulator::stepProfile().body_count == 0);
}

// What a worker asks of a body it has just made is asked of the node, not of the number the simulator will give it. That
// number does not exist until the simulator thread registers the body, so a velocity that was set in between would be
// lost
TOAST_TEST_NAMED("physics", "physics/02-a_worker_configures_a_body_it_just_made", test_physics_02_a_worker_configures_a_body_it_just_made) {
	WorldTestAccess::initThreadPool();
	auto world = WorldTestAccess::createWorld();
	physics::Simulator simulator;
	physics::Simulator::bindToThisThread();

	Body body(*world, "physics::DynamicRigidbody");
	auto dynamic = body.node.as<physics::DynamicRigidbody>();
	assert(dynamic.exists());

	onWorker([&] {
		physics::Simulator::registerRigidbody(*dynamic);
		dynamic->setLinearVelocity(glm::vec3(5.0f, 0.0f, 0.0f));
	});

	physics::Simulator::callTick();
	assert(physics::Simulator::stepProfile().body_count == 1);
	assert(dynamic->linear_velocity.x > 4.0f);
}
