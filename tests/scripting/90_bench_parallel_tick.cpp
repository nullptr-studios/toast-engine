#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <set>
#include <string>
#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>
#include <vector>

using namespace toast::tests::scripting_tests;

namespace {

constexpr int node_count = 96;
constexpr int rounds = 7;
constexpr int frames_per_round = 30;

using Clock = std::chrono::steady_clock;

auto millisecondsSince(Clock::time_point start) -> double {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// Average frame time over one round
template<typename Frame>
auto timeRound(Frame&& frame) -> double {
	const auto start = Clock::now();
	for (int i = 0; i < frames_per_round; ++i) {
		frame();
	}
	return millisecondsSince(start) / frames_per_round;
}

/// Ticks `node_count` scripted nodes that each burn the same CPU time, with the given grouping (0 leaves the runtimes
/// ungrouped), the way the scheduler used to (one pool job per node) and the way it does now (one job per interpreter).
/// The two take turns for several rounds and the best round of each is printed, so a busy moment on the machine does
/// not decide the comparison
void runCase(const char* label, size_t group_size, int iterations = 30000) {
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	const std::string source = "local M = {} M.acc = 0 function M:tick() local a = 0 for i = 1, " + std::to_string(iterations) +
	                           " do a = a + (i % 7) end M.acc = a end return M";

	std::vector<toast::Box<toast::Node>> nodes;
	uint64_t group = 0;
	for (int i = 0; i < node_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "n" + std::to_string(i));
		if (group_size != 0 && static_cast<size_t>(i) % group_size == 0) {
			group = ::scripting::LuaState::newGroup();
		}
		const auto script = makeScript(source, 1000 + i);
		if (group_size == 0) {
			toast::_detail::WorldTestAccess::attachScript(*node, script);
		} else {
			toast::_detail::WorldTestAccess::attachScriptInGroup(*node, script, group);
		}
		nodes.push_back(node);
	}

	std::set<size_t> interpreters;
	for (auto& node : nodes) {
		interpreters.insert(node->scriptRuntime()->stateIndex());
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	// One node on this thread is the cost every node pays, so serial time is that times the node count
	double single = 1e9;
	for (int i = 0; i < 10; ++i) {
		const auto start = Clock::now();
		nodes[0]->scriptRuntime()->call(toast::TickFunctionList::tick);
		single = std::min(single, millisecondsSince(start));
	}

	auto per_node_jobs = [&] {
		std::vector<std::future<void>> futures;
		futures.reserve(nodes.size());
		for (auto& node : nodes) {
			futures.push_back(toast::ThreadPool::push([&node] { node->scriptRuntime()->call(toast::TickFunctionList::tick); }));
		}
		for (auto& future : futures) {
			future.get();
		}
	};
	auto buckets = [&] { toast::_detail::WorldTestAccess::runTickFrame(world); };

	for (int i = 0; i < 5; ++i) {
		per_node_jobs();
		buckets();
	}
	double old_best = 1e9;
	double new_best = 1e9;
	for (int round = 0; round < rounds; ++round) {
		old_best = std::min(old_best, timeRound(per_node_jobs));
		new_best = std::min(new_best, timeRound(buckets));
	}

	const double serial = single * node_count;
	std::printf(
	    "BENCH %-26s %2zu interpreters | per-node jobs %6.2f ms (%4.1fx) | interpreter jobs %6.2f ms (%4.1fx) | serial %6.2f ms\n",
	    label,
	    interpreters.size(),
	    old_best,
	    serial / old_best,
	    new_best,
	    serial / new_best,
	    serial
	);
	std::fflush(stdout);
}

/// The same total work cut into `jobs` pieces that only spin, so what the thread pool itself costs shows without any Lua
/// in the way. The calling thread runs the first piece, like a tick wave does
void spinCase(const char* label, int jobs, double total_ms) {
	const double piece_ms = total_ms / jobs;
	auto spin = [piece_ms] {
		const auto deadline = Clock::now() + std::chrono::duration<double, std::milli>(piece_ms);
		while (Clock::now() < deadline) { }
	};
	auto frame = [&] {
		std::vector<std::future<void>> futures;
		for (int i = 1; i < jobs; ++i) {
			futures.push_back(toast::ThreadPool::push(spin));
		}
		spin();
		for (auto& future : futures) {
			future.get();
		}
	};

	for (int i = 0; i < 5; ++i) {
		frame();
	}
	double best = 1e9;
	for (int round = 0; round < rounds; ++round) {
		best = std::min(best, timeRound(frame));
	}
	const size_t threads = toast::ThreadPool::workerCount() + 1;
	std::printf(
	    "BENCH pool only %-16s %3d jobs of %5.2f ms  frame %6.2f ms  (ideal %5.2f ms)\n",
	    label,
	    jobs,
	    piece_ms,
	    best,
	    piece_ms * static_cast<double>((static_cast<size_t>(jobs) + threads - 1) / threads)
	);
	std::fflush(stdout);
}

std::atomic<size_t> g_ticks_on_caller {0};
std::atomic<size_t> g_ticks_on_workers {0};

/// The C++ tick of the nodes below: notes whether it runs on the thread that runs the frame or on a pool worker. The Lua tick
/// of the same node follows on the same thread, in the same job
void countThread(void* /*node*/) {
	(toast::ThreadPool::onWorkerThread() ? g_ticks_on_workers : g_ticks_on_caller).fetch_add(1, std::memory_order_relaxed);
}

/// Which threads run the ticks of a wave. The thread that runs the frame works on the wave together with the pool, so it
/// takes some of the scripted ticks too, and the smaller the wave the larger its share
void whoRuns(const char* label, int count, int iterations) {
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	const std::string source = "local M = {} M.acc = 0 function M:tick() local a = 0 for i = 1, " + std::to_string(iterations) +
	                           " do a = a + (i % 7) end M.acc = a end return M";
	std::vector<toast::Box<toast::Node>> nodes;
	for (int i = 0; i < count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "w" + std::to_string(i));
		toast::_detail::WorldTestAccess::setTickCallback(*node, &countThread);
		toast::_detail::WorldTestAccess::attachScript(*node, makeScript(source, 2000 + i));
		nodes.push_back(node);
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	for (int i = 0; i < 5; ++i) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}
	g_ticks_on_caller = 0;
	g_ticks_on_workers = 0;
	constexpr int frames = 100;
	const auto start = Clock::now();
	for (int i = 0; i < frames; ++i) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}
	const double frame_ms = millisecondsSince(start) / frames;

	const double caller = static_cast<double>(g_ticks_on_caller.load());
	const double workers = static_cast<double>(g_ticks_on_workers.load());
	std::printf(
	    "BENCH who runs %-22s %4d nodes: frame thread %5.1f%% of the ticks, pool workers %5.1f%% | %7.3f ms per frame\n",
	    label,
	    count,
	    100.0 * caller / (caller + workers),
	    100.0 * workers / (caller + workers),
	    frame_ms
	);
	std::fflush(stdout);
}


/// The init of a tree of scripts, one node after the other against level by level on the pool
void lifecycleCase(const char* label, int branches, int leaves, int iterations) {
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	const std::string source = "local M = {} M.acc = 0 function M:init() local a = 0 for i = 1, " + std::to_string(iterations) +
	                           " do a = a + (i % 7) end M.acc = a end return M";
	std::vector<toast::Box<toast::Node>> nodes;
	uint64_t uid = 8000;
	auto add = [&](const std::string& name, toast::Node* parent, uint64_t group) -> toast::Node* {
		auto node = toast::_detail::WorldTestAccess::createNode(world, name);
		toast::_detail::WorldTestAccess::attachScriptInGroup(*node, makeScript(source, ++uid), group);
		if (parent != nullptr) {
			toast::_detail::WorldTestAccess::attachChild(*parent, *node);
		}
		nodes.push_back(node);
		return &*node;
	};
	toast::Node* root = add("root", nullptr, ::scripting::LuaState::newGroup());
	for (int b = 0; b < branches; ++b) {
		const uint64_t group = ::scripting::LuaState::newGroup();
		toast::Node* branch = add("b" + std::to_string(b), root, group);
		for (int l = 0; l < leaves; ++l) {
			add("l" + std::to_string(b) + "_" + std::to_string(l), branch, group);
		}
	}

	for (int i = 0; i < 3; ++i) {
		toast::_detail::WorldTestAccess::propagateCallTick(*root, toast::TickFunctionList::init);
		toast::_detail::WorldTestAccess::runLifecycle(world, *root, toast::TickFunctionList::init);
	}
	double serial = 1e9;
	double levels = 1e9;
	for (int round = 0; round < 5; ++round) {
		auto start = Clock::now();
		toast::_detail::WorldTestAccess::propagateCallTick(*root, toast::TickFunctionList::init);
		serial = std::min(serial, millisecondsSince(start));
		start = Clock::now();
		toast::_detail::WorldTestAccess::runLifecycle(world, *root, toast::TickFunctionList::init);
		levels = std::min(levels, millisecondsSince(start));
	}
	std::printf(
	    "BENCH lifecycle %-30s %3zu scripts | one node after the other %7.2f ms | level by level %7.2f ms (%4.1fx)\n",
	    label,
	    nodes.size(),
	    serial,
	    levels,
	    serial / levels
	);
	std::fflush(stdout);
}

void writeFile(const std::filesystem::path& path, std::string_view contents) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

/// Instantiates a prefab of scripted nodes from the thread that asks, which builds the scripts one interpreter per job on the
/// pool, and from a pool worker, which builds them one after the other
void instantiateCase(int leaf_count, int chunk_iterations) {
	namespace fs = std::filesystem;
	const fs::path tmp = fs::temp_directory_path() / "toast_bench_instantiate";
	// The asset cache is keyed by UID, so every size gets its own
	const std::string scene_uid = std::format("BenScn{:05}", leaf_count);
	const std::string script_uid = std::format("BenScr{:05}", leaf_count);
	const std::string root_uid = std::format("BenRoot{:04}", leaf_count);
	fs::remove_all(tmp);
	fs::create_directories(tmp / "assets");
	fs::create_directories(tmp / "cache");

	writeFile(
	    tmp / "assets" / "bench.lua",
	    "local M = {} M.loaded = 0 local a = 0 for i = 1, " + std::to_string(chunk_iterations) +
	        " do a = a + (i % 7) end function M:load() M.loaded = 1 end return M"
	);
	std::string scene = std::format("~format @int = 2\n\n[scene_root type=toast::Node]\nm_uid @uid = {}\n", root_uid);
	for (int i = 0; i < leaf_count; ++i) {
		scene += std::format(
		    "\n[leaf_{0} type=toast::Node]\nm_uid @uid = Bn{0:08}x\nm_parent @uid = {1}\nm_scripts @array_uid = {2}\n", i, root_uid, script_uid
		);
	}
	writeFile(tmp / "assets" / "scene.node", scene);
	writeFile(
	    tmp / "cache" / "database.json",
	    std::format(R"({{"node":{{"{0}":"assets://scene.node"}},"script":{{"{1}":"assets://bench.lua"}}}})", scene_uid, script_uid)
	);
	toast::_detail::WorldTestAccess::initAssetManager((tmp / "assets").string(), (tmp / "cache").string());

	auto world = toast::_detail::WorldTestAccess::createWorld();
	const auto file = assets::load<assets::Prefab>(toast::UID(toast::UID::fromString(scene_uid)));
	auto instantiate = [&] {
		toast::INodeOwner::InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		return toast::_detail::WorldTestAccess::instantiate(*world, file, ctx);
	};

	double pooled = 1e9;
	double inline_time = 1e9;
	for (int round = 0; round < 6; ++round) {
		auto start = Clock::now();
		{
			auto tree = instantiate();
		}
		pooled = std::min(pooled, millisecondsSince(start));

		start = Clock::now();
		{
			auto tree = toast::ThreadPool::push(instantiate).get();    // on a worker, so the leaves are built one after the other
		}
		inline_time = std::min(inline_time, millisecondsSince(start));
	}
	std::printf(
	    "BENCH instantiate %3d scripted nodes | built one after the other %7.2f ms | one job per interpreter %7.2f ms (%4.1fx)\n",
	    leaf_count,
	    inline_time,
	    pooled,
	    inline_time / pooled
	);
	std::fflush(stdout);
}

}

// Opt in with TOAST_BENCH=1, it takes a while and prints numbers instead of asserting anything
TOAST_TEST_NAMED("Scripting", "scripting/90_bench_parallel_tick", test_scripting_90_bench_parallel_tick) {
	if (std::getenv("TOAST_BENCH") == nullptr) {
		return;
	}
	luaState();
	std::printf("BENCH %d scripted nodes, %zu workers\n", node_count, toast::ThreadPool::workerCount());
	lifecycleCase("4 x 8, 0.19 ms each", 4, 8, 30000);
	lifecycleCase("12 x 8, 0.19 ms each", 12, 8, 30000);
	lifecycleCase("12 x 8, 20 us each", 12, 8, 3000);
	instantiateCase(24, 150000);
	instantiateCase(96, 150000);
	whoRuns("3 nodes of 0.19 ms", 3, 30000);
	whoRuns("13 nodes of 0.19 ms", 13, 30000);
	whoRuns("96 nodes of 0.19 ms", 96, 30000);
	whoRuns("500 nodes of 6 us", 500, 1000);
	whoRuns("96 nodes, empty ticks", 96, 0);
	runCase("ungrouped (round robin)", 0);
	runCase("groups of 8", 8);
	runCase("one group for everything", node_count);

	// Nothing to compute at all, so what is left is what scheduling a wave costs
	runCase("ungrouped, empty ticks", 0, 0);
	runCase("groups of 8, empty ticks", 8, 0);

	// The pool alone, with the same 20 ms of work cut into pieces of different sizes
	spinCase("13 big jobs", 13, 20.0);
	spinCase("96 small jobs", 96, 20.0);
	spinCase("26 medium jobs", 26, 20.0);
}
