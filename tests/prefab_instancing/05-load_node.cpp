#include "prefab_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/world/world.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace toast;
using namespace toast::tests;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

void writeFile(const std::filesystem::path& path, std::string_view contents) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	assert(out.is_open());
	out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

}    // namespace

// The real load path, end to end through the AssetManager: a scene file on disk that references a
// second prefab file is loaded by UID, expanded (the nested reference resolved from disk too), and
// lands in the cached tree with the right shape.
TOAST_TEST_NAMED("prefab_instancing", "prefab_instancing/05-load_node", test_prefab_instancing_05_load_node) {
	namespace fs = std::filesystem;
	const fs::path tmp = fs::temp_directory_path() / "toast_loadnode_test";
	const fs::path assets_dir = tmp / "assets";
	const fs::path cache_dir = tmp / "cache";

	std::error_code ec;
	fs::remove_all(tmp, ec);
	fs::create_directories(assets_dir);
	fs::create_directories(cache_dir);

	// A child prefab (root + leaf)...
	writeFile(
	    assets_dir / "child_prefab.node",
	    "~format @int = 2\n"
	    "\n[child_root type=toast::Node]\nm_uid @uid = chROOTnode0\n"
	    "\n[child_leaf type=toast::Node]\nm_uid @uid = chLEAFnode0\nm_parent @uid = chROOTnode0\n"
	);

	// ...and a scene whose root contains one instance of the child prefab (a reference chunk).
	writeFile(
	    assets_dir / "scene.node",
	    "~format @int = 2\n"
	    "\n[scene_root type=toast::Node]\nm_uid @uid = scROOTnode0\n"
	    "\n[child_inst type=toast::Node]\nm_uid @uid = scCHILDins0\nm_parent @uid = scROOTnode0\n"
	    "m_source_prefab @uid = Child000000\n"
	);

	// The manifest maps each asset UID to its file.
	writeFile(
	    cache_dir / "database.json",
	    R"({"nodes":{)"
	    R"("Scene000000":"assets://scene.node",)"
	    R"("Child000000":"assets://child_prefab.node"}})"
	);

	WorldTestAccess::initAssetManager(assets_dir.string(), cache_dir.string());

	auto world = WorldTestAccess::createWorld();

	// The real entry point: load by UID. Runs off-thread, lands in the load queue, then the
	// drain (part of tick) activates it into the cached list.
	WorldTestAccess::loadNode(UID(uidOf("Scene000000")));
	WorldTestAccess::waitForLoads(*world);
	WorldTestAccess::drainLoadQueue(*world);

	Box<Node> root = WorldTestAccess::findCached("scene_root");
	assert(root.exists());

	// The scene root is stamped with its own asset (scenes are prefabs).
	assert(root->uid().data() == uidOf("scROOTnode0"));
	assert(root->isInstanceRoot());
	assert(root->sourcePrefab().uid().data() == uidOf("Scene000000"));

	// Its single child is the instance of the child prefab, resolved from disk via the AssetManager.
	const auto& kids = WorldTestAccess::childrenOf(*root);
	assert(kids.size() == 1);
	Box<Node> instance = kids[0];
	assert(instance->uid().data() == uidOf("scCHILDins0"));
	assert(instance->isInstanceRoot());
	assert(instance->sourcePrefab().uid().data() == uidOf("Child000000"));

	// The child prefab's interior was grafted in (loaded from child_prefab.node) and marked.
	const auto& grandkids = WorldTestAccess::childrenOf(*instance);
	assert(grandkids.size() == 1);
	assert(grandkids[0]->uid().data() == uidOf("chLEAFnode0"));
	assert(WorldTestAccess::isPrefabInterior(*grandkids[0]));

	fs::remove_all(tmp, ec);
}
