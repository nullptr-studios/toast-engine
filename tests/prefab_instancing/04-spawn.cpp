#include "prefab_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <memory>
#include <set>
#include <variant>

using namespace toast;
using namespace toast::tests;
using assets::Prefab;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

// Nested prefabs S -> A -> B for the namespace-query tests.
const std::string PREFAB_B =
    "[b_root type=toast::Node]\nm_uid @uid = bROOTnode00\n"
    "\n[b_child type=toast::Node]\nm_uid @uid = bCHILDnode0\nm_parent @uid = bROOTnode00\n";
const std::string PREFAB_A =
    "[a_root type=toast::Node]\nm_uid @uid = aROOTnode00\n"
    "\n[b_inst type=toast::Node]\nm_uid @uid = placeBinA00\nm_parent @uid = aROOTnode00\nm_source_prefab @uid = BBBBBBBBBBB\n";
const std::string PREFAB_S =
    "[s_root type=toast::Node]\nm_uid @uid = sROOTnode00\n"
    "\n[a_inst type=toast::Node]\nm_uid @uid = placeAinS00\nm_parent @uid = sROOTnode00\nm_source_prefab @uid = AssetA00000\n";

// A simple prefab to spawn at runtime.
const std::string PREFAB_P =
    "[p_root type=toast::Node]\nm_uid @uid = pROOTnode00\n"
    "\n[p_child type=toast::Node]\nm_uid @uid = pCHILDnode0\nm_parent @uid = pROOTnode00\n";

auto scheduledTickUids(World& world) -> std::set<uint64_t> {
	std::set<uint64_t> uids;
	for (const auto& wave : WorldTestAccess::tickSchedule(world).tick) {
		for (const auto& item : wave) {
			if (std::holds_alternative<Box<Node>>(item)) {
				uids.insert(std::get<Box<Node>>(item)->uid().data());
			} else {
				for (const auto& n : std::get<_detail::NodeCluster>(item).nodes) {
					uids.insert(n->uid().data());
				}
			}
		}
	}
	return uids;
}

}    // namespace

TOAST_TEST_NAMED("prefab_instancing", "prefab_instancing/04-spawn", test_prefab_instancing_04_spawn) {
	// --- Namespace queries over nested instances (S -> A -> B) --------------------------------
	{
		PrefabStore store;
		store.add("BBBBBBBBBBB", PREFAB_B);
		store.add("AssetA00000", PREFAB_A);
		store.add("SceneS00000", PREFAB_S);

		auto world = WorldTestAccess::createWorld();
		NodeOwner::InstantiateContext ctx;
		ctx.resolver = store.resolver();

		Box<Node> root = WorldTestAccess::instantiate(*world, store.handle("SceneS00000"), ctx);
		assert(root.exists());
		WorldTestAccess::setWorldRoot(*world, *root);

		Box<Node> a_inst = WorldTestAccess::childrenOf(*root)[0];
		Box<Node> b_inst = WorldTestAccess::childrenOf(*a_inst)[0];
		assert(a_inst->uid().data() == uidOf("placeAinS00"));
		assert(b_inst->uid().data() == uidOf("placeBinA00"));
		assert(a_inst->isInstanceRoot() && b_inst->isInstanceRoot());

		// World scope sees the top-level instance but NOT the interior of a nested one.
		assert(WorldTestAccess::findNode(UID(uidOf("placeAinS00"))).rid() == a_inst.rid());
		assert(not WorldTestAccess::findNode(UID(uidOf("placeBinA00"))).exists());

		// Scoped to the instance root, the interior instance is reachable (one boundary crossed).
		assert(WorldTestAccess::findNode(UID(uidOf("placeBinA00")), &*a_inst).rid() == b_inst.rid());

		// The public Node::find mirrors the same opacity: from the scene root the top instance is
		// visible but the nested interior is not, while scoping to the instance root reveals it.
		assert(root->find("placeAinS00").rid() == a_inst.rid());
		assert(not root->find("placeBinA00").exists());
		assert(a_inst->find("placeBinA00").rid() == b_inst.rid());

		// uidPath / findNode(path) are mutual inverses across two boundaries.
		std::string path = WorldTestAccess::uidPath(*b_inst);
		Box<Node> resolved = WorldTestAccess::findNode(path);
		assert(resolved.exists());
		assert(resolved.rid() == b_inst.rid());
		// The path crosses two boundaries below the root: root / a_inst / b_inst.
		assert(std::count(path.begin(), path.end(), '/') == 2);
	}

	// --- Runtime spawn: two instances of the same prefab under one parent --------------------
	{
		PrefabStore store;
		store.add("AssetP00000", PREFAB_P);

		auto world = WorldTestAccess::createWorld();
		Box<Node> parent = WorldTestAccess::createNode(*world, "spawn_parent", NodeState::root);

		NodeOwner::InstantiateContext ctx1;
		ctx1.resolver = store.resolver();
		Box<Node> first = WorldTestAccess::spawnSync(*world, store.handle("AssetP00000"), *parent, ctx1);

		NodeOwner::InstantiateContext ctx2;
		ctx2.resolver = store.resolver();
		Box<Node> second = WorldTestAccess::spawnSync(*world, store.handle("AssetP00000"), *parent, ctx2);

		assert(first.exists() && second.exists());

		// Both attached under the parent, each with a distinct, freshly generated placement UID.
		assert(WorldTestAccess::childrenOf(*parent).size() == 2);
		assert(first->uid().data() != second->uid().data());
		assert(first->isInstanceRoot() && second->isInstanceRoot());
		assert(first->sourcePrefab().uid().data() == uidOf("AssetP00000"));

		// Both spawned roots participate in the tick schedule (toast::Node carries a tick stub).
		std::set<uint64_t> scheduled = scheduledTickUids(*world);
		assert(scheduled.contains(first->uid().data()));
		assert(scheduled.contains(second->uid().data()));
	}
}
