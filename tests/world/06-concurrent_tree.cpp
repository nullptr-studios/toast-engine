#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <atomic>
#include <cassert>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace toast;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

/// A world with a root, two parents with some children each, and one node that sits under the first parent
struct Scene {
	WorldTestAccess::WorldPtr world = WorldTestAccess::createWorld();
	Box<Node> root;
	Box<Node> a;
	Box<Node> b;
	Box<Node> traveller;
	std::vector<Box<Node>> filler;

	Scene() {
		root = WorldTestAccess::createNode(*world, "root");
		a = WorldTestAccess::createNode(*world, "a");
		b = WorldTestAccess::createNode(*world, "b");
		WorldTestAccess::attachChild(*root, *a);
		WorldTestAccess::attachChild(*root, *b);
		for (int i = 0; i < 20; ++i) {
			filler.push_back(WorldTestAccess::createNode(*world, "fa" + std::to_string(i)));
			WorldTestAccess::attachChild(*a, *filler.back());
			filler.push_back(WorldTestAccess::createNode(*world, "fb" + std::to_string(i)));
			WorldTestAccess::attachChild(*b, *filler.back());
		}
		traveller = WorldTestAccess::createNode(*world, "traveller");
		WorldTestAccess::attachChild(*a, *traveller);
		WorldTestAccess::setWorldRoot(*world, *root);
	}
};

}    // namespace

// The scripts of a wave or of a loader read the node tree while the thread that owns it changes it. Moving a node is one
// step for them: they find it under the parent it had or under the one it moves to, never in neither
TOAST_TEST_NAMED("world", "world/06-concurrent_tree", test_world_06) {
	WorldTestAccess::initThreadPool();
	Scene scene;
	const UID uid = scene.traveller->uid();

	std::atomic<bool> stop {false};
	std::atomic<int> misses {0};
	std::atomic<long> reads {0};
	std::thread reader([&] {
		while (!stop) {
			if (!WorldTestAccess::findNode(uid).exists()) {
				++misses;
			}
			if (!scene.root->find("traveller").exists()) {
				++misses;
			}
			++reads;
		}
	});

	for (int i = 0; i < 400; ++i) {
		WorldTestAccess::moveToChild(*scene.world, *scene.traveller, (i % 2 == 0) ? *scene.b : *scene.a);
	}
	stop = true;
	reader.join();

	assert(reads > 0);
	assert(misses == 0);
}

// Scripts create nodes on the threads that run them, so several threads can add children under one parent at once.
// Every one of them has to end up in the parent, once
TOAST_TEST_NAMED("world", "world/06b-concurrent_creates", test_world_06b) {
	WorldTestAccess::initThreadPool();
	Scene scene;
	const size_t before = WorldTestAccess::childrenOf(*scene.a).size();

	constexpr int thread_count = 4;
	constexpr int per_thread = 150;
	std::vector<std::thread> threads;
	for (int t = 0; t < thread_count; ++t) {
		threads.emplace_back([&] {
			for (int i = 0; i < per_thread; ++i) {
				[[maybe_unused]] const Box<Node> created = scene.a->create();    // outside the assert, which release builds drop
				assert(created.exists());
			}
		});
	}
	for (auto& thread : threads) {
		thread.join();
	}

	const auto& children = WorldTestAccess::childrenOf(*scene.a);
	assert(children.size() == before + thread_count * per_thread);
	std::set<uint64_t> uids;
	for (const auto& child : children) {
		assert(child.exists());
		uids.insert(child->uid().data());
	}
	assert(uids.size() == children.size());
}
