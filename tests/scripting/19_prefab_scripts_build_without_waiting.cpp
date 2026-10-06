#include "prefab_instancing/prefab_test_helpers.hpp"
#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_runtime.hpp"
#include "toast/thread_pool.hpp"

#include <any>
#include <cassert>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>
#include <vector>

using namespace toast;
using namespace toast::tests;
using namespace toast::tests::scripting_tests;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

constexpr int k_leaf_count = 24;

void writeFile(const std::filesystem::path& path, std::string_view contents) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	assert(out.is_open());
	out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// Running the chunk is what takes the time, like a script with a big setup would
constexpr const char* k_slow_script = R"lua(
local M = {}
M.loaded = 0
local acc = 0
for i = 1, 150000 do acc = acc + (i % 7) end
M.checksum = acc
function M:load() M.loaded = 1 end
return M
)lua";

/// A prefab file on disk, loaded through the asset manager, with a root and k_leaf_count children that each have the slow script
auto sceneWithScriptedLeaves() -> assets::Handle<assets::Prefab> {
	namespace fs = std::filesystem;
	const fs::path tmp = fs::temp_directory_path() / "toast_scripted_prefab_test";
	const fs::path assets_dir = tmp / "assets";
	const fs::path cache_dir = tmp / "cache";

	std::error_code ec;
	fs::remove_all(tmp, ec);
	fs::create_directories(assets_dir);
	fs::create_directories(cache_dir);

	writeFile(assets_dir / "slow.lua", k_slow_script);

	std::string scene = "~format @int = 2\n\n[scene_root type=toast::Node]\nm_uid @uid = SlowRoot000\n";
	for (int i = 0; i < k_leaf_count; ++i) {
		scene += std::format(
		    "\n[leaf_{0} type=toast::Node]\nm_uid @uid = Nd{0:08}x\nm_parent @uid = SlowRoot000\nm_scripts @array_uid = SlowScrpt00\n", i
		);
	}
	writeFile(assets_dir / "scene.node", scene);
	writeFile(
	    cache_dir / "database.json", R"({"node":{"SlowScene00":"assets://scene.node"},"script":{"SlowScrpt00":"assets://slow.lua"}})"
	);
	WorldTestAccess::initAssetManager(assets_dir.string(), cache_dir.string());

	const auto file = assets::load<assets::Prefab>(UID(uidOf("SlowScene00")));
	assert(file.hasValue());
	return file;
}

auto instantiateScene(World& world, const assets::Handle<assets::Prefab>& file) -> Box<Node> {
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](UID id) { return assets::load<assets::Prefab>(id); };
	return WorldTestAccess::instantiate(world, file, ctx);
}

/// Every leaf has its scripts, load() ran on it, and the leaves are in file order. Returns how many sit on each interpreter
auto checkLeaves(const Box<Node>& root) -> std::map<size_t, int> {
	assert(root.exists());
	const auto& children = WorldTestAccess::childrenOf(*root);
	assert(children.size() == k_leaf_count);

	std::map<size_t, int> per_interpreter;
	for (int i = 0; i < k_leaf_count; ++i) {
		Box<Node> leaf = children[static_cast<size_t>(i)];
		assert(leaf->name() == std::format("leaf {}", i));
		assert(leaf->scriptRuntime() != nullptr);
		assert(std::any_cast<int>(leaf->scriptRuntime()->getVar("loaded")) == 1);
		++per_interpreter[leaf->scriptRuntime()->stateIndex()];
	}
	return per_interpreter;
}

}

// The leaves of a prefab run Lua while they are built (their scripts are created and load() runs), and the ones of one prefab
// instance share interpreters. They are built one interpreter per job, so no worker ever waits for an interpreter that
// another worker of the same instance has
TOAST_TEST_NAMED("Scripting", "scripting/19_prefab_scripts_build_without_waiting", test_scripting_19_prefab_scripts_build_without_waiting) {
	luaState();
	const auto file = sceneWithScriptedLeaves();
	auto world = WorldTestAccess::createWorld();

	const auto waits_before = ::scripting::LuaState::get().waitCount();
	Box<Node> root = instantiateScene(*world, file);
	const auto waits = ::scripting::LuaState::get().waitCount() - waits_before;

	const auto per_interpreter = checkLeaves(root);

	// The scripts of the instance are placed the way they always were: a chunk of eight to an interpreter
	assert(per_interpreter.size() == 3);
	for (const auto& [interpreter, count] : per_interpreter) {
		assert(count == 8);
	}

	assert(waits == 0);
}

// A pool worker cannot wait for the rest of the pool, so a prefab that is built from one (a script that spawns it) builds its
// leaves right there, one after the other
TOAST_TEST_NAMED("Scripting", "scripting/19b_a_pool_worker_builds_the_scripts_of_a_prefab_itself", test_scripting_19b_a_pool_worker_builds_the_scripts_of_a_prefab_itself) {
	luaState();
	const auto file = sceneWithScriptedLeaves();
	auto world = WorldTestAccess::createWorld();

	Box<Node> root = ThreadPool::push([&] { return instantiateScene(*world, file); }).get();

	const auto per_interpreter = checkLeaves(root);
	assert(per_interpreter.size() == 3);
}

// A thread that owns an interpreter cannot wait for another one, so a prefab built from it puts every script on the one it
// has
TOAST_TEST_NAMED("Scripting", "scripting/19c_a_thread_that_owns_an_interpreter_builds_the_scripts_on_it", test_scripting_19c_a_thread_that_owns_an_interpreter_builds_the_scripts_on_it) {
	luaState();
	const auto file = sceneWithScriptedLeaves();
	auto world = WorldTestAccess::createWorld();

	const size_t owned = ::scripting::LuaState::get().poolSize() - 1;
	Box<Node> root;
	{
		auto guard = ::scripting::LuaState::get().lock(owned);
		assert(guard);
		root = instantiateScene(*world, file);
	}

	const auto per_interpreter = checkLeaves(root);
	assert(per_interpreter.size() == 1);
	assert(per_interpreter.begin()->first == owned);
}
