#include "asset_db_fixture.hpp"
#include "prefab_instancing/prefab_test_helpers.hpp"
#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>

using namespace toast;
using namespace toast::tests;
using namespace toast::tests::scripting_tests;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

void writeFile(const std::filesystem::path& path, std::string_view contents) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	assert(out.is_open());
	out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

constexpr const char* k_script = R"lua(
local M = {}
M.value = 1
M.loaded = 0
function M:load() self.loaded = 1 end
return M
)lua";

// The inner prefab's root has no scripts; the scene's instance of it adds one and overrides one of its vars
auto sceneWithOverriddenInstance() -> assets::Handle<assets::Prefab> {
	namespace fs = std::filesystem;
	const fs::path tmp = fs::temp_directory_path() / "toast_nested_script_override_test";
	const fs::path assets_dir = tmp / "assets";
	const fs::path cache_dir = tmp / "cache";

	std::error_code ec;
	fs::remove_all(tmp, ec);
	fs::create_directories(assets_dir);
	fs::create_directories(cache_dir);

	writeFile(assets_dir / "added.lua", k_script);
	writeFile(
	    assets_dir / "inner.node",
	    "~format @int = 2\n\n[inner_root type=toast::Node]\nm_uid @uid = InnerRoot00\n"
	    "\n[inner_child type=toast::Node]\nm_uid @uid = InnerChld00\nm_parent @uid = InnerRoot00\n"
	);
	writeFile(
	    assets_dir / "scene.node",
	    "~format @int = 2\n\n[scene_root type=toast::Node]\nm_uid @uid = SceneRoot00\n"
	    "\n[floor type=toast::Node]\nm_uid @uid = FloorInst00\nm_parent @uid = SceneRoot00\nm_source_prefab @uid = InnerPfab00\n"
	    "m_scripts @array_uid = AddedScrp00\n~lua 0:value = 7\n"
	);
	test_support::writeAssetDb(
	    cache_dir / "assets.db",
	    {{"SceneNode00", "node", "assets", "scene.node"},
	     {"InnerPfab00", "node", "assets", "inner.node"},
	     {"AddedScrp00", "script", "assets", "added.lua"}}
	);
	WorldTestAccess::initAssetManager(assets_dir.string(), cache_dir.string());

	const auto file = assets::load<assets::Prefab>(UID(uidOf("SceneNode00")));
	assert(file.hasValue());
	return file;
}

}

// A nested instance whose root gets scripts only from the outer file (the inner prefab's root has none) must still run them,
// with the outer file's lua overrides applied. Before, applyFields only wrote m_scripts and the root never got a runtime
TOAST_TEST_NAMED("Scripting", "scripting/22_nested_instance_script_override", test_scripting_22_nested_instance_script_override) {
	luaState();
	const auto file = sceneWithOverriddenInstance();
	auto world = WorldTestAccess::createWorld();

	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](UID id) { return assets::load<assets::Prefab>(id); };
	Box<Node> root = WorldTestAccess::instantiate(*world, file, ctx);
	assert(root.exists());

	const auto& children = WorldTestAccess::childrenOf(*root);
	assert(children.size() == 1);
	Box<Node> floor = children.front();

	assert(floor->scriptRuntime() != nullptr);
	assert(std::any_cast<int>(floor->scriptRuntime()->getVar("loaded")) == 1);
	assert(std::any_cast<int>(floor->scriptRuntime()->getVar("value")) == 7);
	// the inner prefab's own child is still there
	assert(WorldTestAccess::childrenOf(*floor).size() == 1);
}
