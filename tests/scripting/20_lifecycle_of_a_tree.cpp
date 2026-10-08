#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_runtime.hpp"
#include "toast/thread_pool.hpp"

#include <algorithm>
#include <any>
#include <atomic>
#include <cassert>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace toast::tests::scripting_tests;
using Stage = toast::TickFunctionList;

namespace {

std::mutex g_order_mutex;
std::vector<const void*> g_native_order;         // the C++ functions of the nodes, in the order they ran
std::atomic<size_t> g_native_on_workers {0};     // how many of them ran on a pool worker

/// The C++ function of a node for the stage under test
void recordNative(void* node) {
	std::scoped_lock lock(g_order_mutex);
	g_native_order.push_back(node);
	if (toast::ThreadPool::onWorkerThread()) {
		++g_native_on_workers;
	}
}

void resetRecords() {
	std::scoped_lock lock(g_order_mutex);
	g_native_order.clear();
	g_native_on_workers = 0;
}

/// What a script of the tree does in its stage: stay busy for a few milliseconds, and note when
auto scriptFor(const char* function_name) -> std::string {
	return std::string("local M = {}\nM.t0 = 0.0\nM.t1 = 0.0\nM.runs = 0\nfunction M:") + function_name +
	       "()\n"
	       "    M.t0 = os.clock()\n"
	       "    local deadline = M.t0 + 0.004\n"
	       "    while os.clock() < deadline do end\n"
	       "    M.t1 = os.clock()\n"
	       "    M.runs = M.runs + 1\n"
	       "end\nreturn M\n";
}

auto asDouble(const std::any& value) -> double {
	if (const auto* d = std::any_cast<double>(&value)) {
		return *d;
	}
	if (const auto* f = std::any_cast<float>(&value)) {
		return static_cast<double>(*f);
	}
	if (const auto* i = std::any_cast<int>(&value)) {
		return static_cast<double>(*i);
	}
	if (const auto* l = std::any_cast<int64_t>(&value)) {
		return static_cast<double>(*l);
	}
	assert(false && "a number was expected");
	return 0.0;
}

struct Tree {
	std::vector<toast::Box<toast::Node>> nodes;           // in depth-first order, the root first
	std::vector<std::pair<size_t, size_t>> edges;         // parent, child
	std::unordered_map<const void*, size_t> index_of;     // by address

	auto root() -> toast::Box<toast::Node>& { return nodes.front(); }
};

/// A root with `branches` children, each the root of its own prefab instance with `leaves` nodes below it. Every node has a
/// C++ function and, when `scripted`, a script, for `stage`. The scripts of one branch share a script group
auto buildTree(toast::World& world, Stage stage, const char* function_name, int branches, int leaves, bool scripted) -> Tree {
	static uint64_t script_uid = 5000;
	const std::string source = scriptFor(function_name);
	Tree tree;
	auto add = [&](const std::string& name, int parent, uint64_t group) -> size_t {
		auto node = toast::_detail::WorldTestAccess::createNode(world, name);
		toast::_detail::WorldTestAccess::setStageCallback(*node, stage, &recordNative);
		toast::_detail::WorldTestAccess::markEnabled(*node);
		if (scripted) {
			toast::_detail::WorldTestAccess::attachScriptInGroup(*node, makeScript(source, ++script_uid), group);
		}
		const size_t index = tree.nodes.size();
		if (parent >= 0) {
			toast::_detail::WorldTestAccess::attachChild(*tree.nodes[static_cast<size_t>(parent)], *node);
			tree.edges.emplace_back(static_cast<size_t>(parent), index);
		}
		tree.index_of[&*node] = index;
		tree.nodes.push_back(node);
		return index;
	};

	const size_t root = add("root", -1, ::scripting::LuaState::newGroup());
	for (int b = 0; b < branches; ++b) {
		const uint64_t group = ::scripting::LuaState::newGroup();
		const size_t branch = add("branch" + std::to_string(b), static_cast<int>(root), group);
		for (int l = 0; l < leaves; ++l) {
			add("leaf" + std::to_string(b) + "_" + std::to_string(l), static_cast<int>(branch), group);
		}
	}
	return tree;
}

auto scriptTimes(toast::Box<toast::Node>& node) -> std::pair<double, double> {
	auto* runtime = node->scriptRuntime();
	return {asDouble(runtime->getVar("t0")), asDouble(runtime->getVar("t1"))};
}

/// True when two nodes that live on different interpreters were inside their scripts at the same time
auto anyOverlap(Tree& tree) -> bool {
	for (size_t a = 0; a < tree.nodes.size(); ++a) {
		for (size_t b = a + 1; b < tree.nodes.size(); ++b) {
			if (tree.nodes[a]->scriptRuntime()->stateIndex() == tree.nodes[b]->scriptRuntime()->stateIndex()) {
				continue;
			}
			const auto [a0, a1] = scriptTimes(tree.nodes[a]);
			const auto [b0, b1] = scriptTimes(tree.nodes[b]);
			if (a0 < b1 && b0 < a1) {
				return true;
			}
		}
	}
	return false;
}

void checkRanOnce(Tree& tree) {
	for (auto& node : tree.nodes) {
		assert(std::any_cast<int>(node->scriptRuntime()->getVar("runs")) == 1);
	}
	std::scoped_lock lock(g_order_mutex);
	assert(g_native_order.size() == tree.nodes.size());
}

/// Every node after its parent: in its C++ function, and in its script
void checkParentsFirst(Tree& tree) {
	std::scoped_lock lock(g_order_mutex);
	std::vector<size_t> position(tree.nodes.size(), 0);
	for (size_t i = 0; i < g_native_order.size(); ++i) {
		position[tree.index_of.at(g_native_order[i])] = i;
	}
	for (const auto& [parent, child] : tree.edges) {
		assert(position[parent] < position[child]);
	}
	for (const auto& [parent, child] : tree.edges) {
		assert(scriptTimes(tree.nodes[child]).first >= scriptTimes(tree.nodes[parent]).second);
	}
}

void runStageTest(Stage stage, const char* function_name) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	// 1 + 4 + 4 * 8 = 37 nodes with scripts, on a handful of interpreters
	Tree tree = buildTree(world, stage, function_name, 4, 8, true);
	resetRecords();
	toast::_detail::WorldTestAccess::runLifecycle(world, *tree.root(), stage);

	checkRanOnce(tree);
	checkParentsFirst(tree);
	// The C++ functions are not written for several threads, they stay on the thread that asked for the stage
	assert(g_native_on_workers == 0);
	// The scripts of nodes on different interpreters run at the same time
	assert(anyOverlap(tree));
}

}

// A tree with many scripts has them run their init on the pool, a job per interpreter, with a node's script always after the
// one of its parent. The C++ functions of the nodes stay on the thread that asked, one after the other
TOAST_TEST_NAMED("Scripting", "scripting/20_init_of_a_tree_runs_scripts_in_parallel", test_scripting_20_init_of_a_tree_runs_scripts_in_parallel) {
	runStageTest(Stage::init, "init");
}

TOAST_TEST_NAMED("Scripting", "scripting/20b_begin_of_a_tree_runs_scripts_in_parallel", test_scripting_20b_begin_of_a_tree_runs_scripts_in_parallel) {
	runStageTest(Stage::begin, "begin");
}

TOAST_TEST_NAMED("Scripting", "scripting/20c_on_enable_of_a_tree_runs_scripts_in_parallel", test_scripting_20c_on_enable_of_a_tree_runs_scripts_in_parallel) {
	runStageTest(Stage::on_enable, "onEnable");
}

// A tree with a handful of scripts is not worth handing out, it keeps the order it always had: each node followed by
// everything below it
TOAST_TEST_NAMED("Scripting", "scripting/20d_a_small_tree_keeps_the_depth_first_order", test_scripting_20d_a_small_tree_keeps_the_depth_first_order) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	Tree tree = buildTree(world, Stage::init, "init", 2, 2, true);    // 1 + 2 + 4 = 7 nodes
	resetRecords();
	toast::_detail::WorldTestAccess::runLifecycle(world, *tree.root(), Stage::init);

	checkRanOnce(tree);
	std::scoped_lock lock(g_order_mutex);
	for (size_t i = 0; i < tree.nodes.size(); ++i) {
		assert(g_native_order[i] == &*tree.nodes[i]);    // tree.nodes is in depth-first order
	}
}

// The nodes the scripts create while they run have their own stage, so the walk must not give them a second one, and
// several scripts adding to the tree at once must not stall each other
TOAST_TEST_NAMED("Scripting", "scripting/20e_scripts_that_create_nodes_during_init", test_scripting_20e_scripts_that_create_nodes_during_init) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	Tree tree = buildTree(world, Stage::init, "init", 4, 8, false);
	static uint64_t script_uid = 7000;
	const std::string creating = R"lua(
local M = {}
M.runs = 0
function M:init()
    self:create("toast::Node")
    M.runs = M.runs + 1
end
return M
)lua";
	for (auto& node : tree.nodes) {
		toast::_detail::WorldTestAccess::attachScriptInGroup(*node, makeScript(creating, ++script_uid), ::scripting::LuaState::newGroup());
	}
	const size_t before = tree.nodes.size();

	resetRecords();
	toast::_detail::WorldTestAccess::runLifecycle(world, *tree.root(), Stage::init);

	for (auto& node : tree.nodes) {
		assert(std::any_cast<int>(node->scriptRuntime()->getVar("runs")) == 1);
	}
	// Every node got exactly one new child, and the walk did not visit it
	size_t created = 0;
	for (auto& node : tree.nodes) {
		created += toast::_detail::WorldTestAccess::childrenOf(*node).size();
	}
	assert(created == before - 1 + before);    // the edges of the tree plus the one child each node made
	std::scoped_lock lock(g_order_mutex);
	assert(g_native_order.size() == before);
}
