#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/uid.hpp>
#include <unordered_map>

using namespace toast;
using namespace assets;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

// A pure in-memory prefab store: maps an asset UID to a parsed Prefab and hands instantiate a
// resolver, so nested-prefab expansion can be tested without an AssetManager.
struct PrefabStore {
	std::unordered_map<uint64_t, std::unique_ptr<Prefab>> assets;

	void add(std::string_view uid_str, const std::string& text) {
		std::stringstream ss(text);
		assets[UID::fromString(uid_str)] = std::make_unique<Prefab>(ss);
	}

	auto resolver() {
		return [this](toast::UID id) -> Handle<Prefab> {
			auto it = assets.find(id.data());
			return it != assets.end() ? Handle<Prefab>(it->second.get(), id, "") : Handle<Prefab>(nullptr, id, "");
		};
	}

	auto handle(std::string_view uid_str) -> Handle<Prefab> {
		UID id(UID::fromString(uid_str));
		return Handle<Prefab>(assets.at(id.data()).get(), id, "");
	}
};

// Asset UIDs (the prefab files) and node UIDs (chunks within them). All 11-char base64url.
constexpr const char* A_ASSET = "AssetA00000";
constexpr const char* B_ASSET = "BBBBBBBBBBB";

// Prefab B: a root with one child.
const std::string PREFAB_B =
    "[b_root type=toast::Node]\n"
    "m_uid @uid = bROOTnode00\n"
    "\n"
    "[b_child type=toast::Node]\n"
    "m_uid @uid = bCHILDnode0\n"
    "m_parent @uid = bROOTnode00\n";

auto uidOf(const char* s) -> uint64_t {
	return UID::fromString(s);
}

}    // namespace

// Nested instance: A contains one instance of B. The instance root takes the placement chunk's
// UID, points at B as its source prefab, and B's interior is grafted in and marked.
TOAST_TEST_NAMED("prefab_instancing", "prefab_instancing/02-instantiate", test_prefab_instancing_02) {
	// --- nested: A contains one instance of B ---
	{
	PrefabStore store;
	store.add(B_ASSET, PREFAB_B);
	store.add(
	    A_ASSET,
	    "[a_root type=toast::Node]\n"
	    "m_uid @uid = aROOTnode00\n"
	    "\n"
	    "[b_instance type=toast::Node]\n"
	    "m_uid @uid = PLACEMENT00\n"
	    "m_parent @uid = aROOTnode00\n"
	    "m_source_prefab @uid = BBBBBBBBBBB\n"
	);

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = store.resolver();

	Box<Node> root = WorldTestAccess::instantiate(*world, store.handle(A_ASSET), ctx);
	assert(root.exists());

	// Outer root: a plain scene root stamped with its own source prefab (A).
	assert(root->uid().data() == uidOf("aROOTnode00"));
	assert(root->isInstanceRoot());
	assert(root->sourcePrefab().uid().data() == uidOf(A_ASSET));
	assert(not WorldTestAccess::isPrefabInterior(*root));

	// The single child is the instance root of B.
	const auto& root_children = WorldTestAccess::childrenOf(*root);
	assert(root_children.size() == 1);
	Box<Node> instance = root_children[0];

	// Grafted instance root takes the OUTER placement chunk's UID, not B's own root UID.
	assert(instance->uid().data() == uidOf("PLACEMENT00"));
	assert(instance->name() == "b instance");    // name overridden by the placement chunk header
	assert(instance->isInstanceRoot());
	assert(instance->sourcePrefab().uid().data() == uidOf(B_ASSET));
	assert(not WorldTestAccess::isPrefabInterior(*instance));    // an instance root is not interior

	// B's child is grafted under the instance root, keeps B's own UID, and is marked interior.
	const auto& instance_children = WorldTestAccess::childrenOf(*instance);
	assert(instance_children.size() == 1);
	Box<Node> interior = instance_children[0];
	assert(interior->uid().data() == uidOf("bCHILDnode0"));
	assert(WorldTestAccess::isPrefabInterior(*interior));
	assert(not interior->isInstanceRoot());
	}

	// --- siblings: two instances of B under one parent (distinct roots, shared interior UIDs) ---
	{
	PrefabStore store;
	store.add(B_ASSET, PREFAB_B);
	store.add(
	    A_ASSET,
	    "[a_root type=toast::Node]\n"
	    "m_uid @uid = aROOTnode00\n"
	    "\n"
	    "[inst_one type=toast::Node]\n"
	    "m_uid @uid = Place1ment0\n"
	    "m_parent @uid = aROOTnode00\n"
	    "m_source_prefab @uid = BBBBBBBBBBB\n"
	    "\n"
	    "[inst_two type=toast::Node]\n"
	    "m_uid @uid = Place2ment0\n"
	    "m_parent @uid = aROOTnode00\n"
	    "m_source_prefab @uid = BBBBBBBBBBB\n"
	);

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = store.resolver();

	Box<Node> root = WorldTestAccess::instantiate(*world, store.handle(A_ASSET), ctx);
	assert(root.exists());

	const auto& kids = WorldTestAccess::childrenOf(*root);
	assert(kids.size() == 2);

	// Distinct instance-root UIDs.
	assert(kids[0]->uid().data() != kids[1]->uid().data());
	assert(kids[0]->uid().data() == uidOf("Place1ment0"));
	assert(kids[1]->uid().data() == uidOf("Place2ment0"));

	// Identical interior UIDs across the two instances (this is intentional).
	uint64_t interior0 = WorldTestAccess::childrenOf(*kids[0])[0]->uid().data();
	uint64_t interior1 = WorldTestAccess::childrenOf(*kids[1])[0]->uid().data();
	assert(interior0 == uidOf("bCHILDnode0"));
	assert(interior0 == interior1);
	}

	// --- missing asset: reference degrades to an unresolved instance root ---
	{
	PrefabStore store;
	store.add(
	    A_ASSET,
	    "[a_root type=toast::Node]\n"
	    "m_uid @uid = aROOTnode00\n"
	    "\n"
	    "[dangling type=toast::Node]\n"
	    "m_uid @uid = PLACEMENT00\n"
	    "m_parent @uid = aROOTnode00\n"
	    "m_source_prefab @uid = BBBBBBBBBBB\n"    // B is not in the store
	);

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = store.resolver();

	Box<Node> root = WorldTestAccess::instantiate(*world, store.handle(A_ASSET), ctx);
	assert(root.exists());

	const auto& kids = WorldTestAccess::childrenOf(*root);
	assert(kids.size() == 1);
	Box<Node> unresolved = kids[0];

	// Still an instance root (UID preserved), but with no resolved asset and no grafted interior.
	assert(unresolved->uid().data() == uidOf("PLACEMENT00"));
	assert(unresolved->isInstanceRoot());
	assert(unresolved->sourcePrefab().uid().data() == uidOf(B_ASSET));
	assert(not unresolved->sourcePrefab().hasValue());
	assert(WorldTestAccess::childrenOf(*unresolved).empty());
	}

	// --- cycle A -> B -> A: detected on revisit, degrades to unresolved ---
	{
	PrefabStore store;
	store.add(
	    A_ASSET,
	    "[a_root type=toast::Node]\n"
	    "m_uid @uid = aROOTnode00\n"
	    "\n"
	    "[b_in_a type=toast::Node]\n"
	    "m_uid @uid = PLACEMENTab\n"
	    "m_parent @uid = aROOTnode00\n"
	    "m_source_prefab @uid = BBBBBBBBBBB\n"
	);
	store.add(
	    B_ASSET,
	    "[b_root type=toast::Node]\n"
	    "m_uid @uid = bROOTnode00\n"
	    "\n"
	    "[a_in_b type=toast::Node]\n"
	    "m_uid @uid = PLACEMENTba\n"
	    "m_parent @uid = bROOTnode00\n"
	    "m_source_prefab @uid = AssetA00000\n"    // back-reference to A → cycle
	);

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = store.resolver();

	Box<Node> root = WorldTestAccess::instantiate(*world, store.handle(A_ASSET), ctx);
	assert(root.exists());
	assert(root->uid().data() == uidOf("aROOTnode00"));

	// A's instance of B expands fine...
	Box<Node> b_instance = WorldTestAccess::childrenOf(*root)[0];
	assert(b_instance->uid().data() == uidOf("PLACEMENTab"));
	assert(b_instance->sourcePrefab().uid().data() == uidOf(B_ASSET));

	// ...but B's back-reference to A is broken by the cycle guard: unresolved, no recursion.
	Box<Node> a_again = WorldTestAccess::childrenOf(*b_instance)[0];
	assert(a_again->uid().data() == uidOf("PLACEMENTba"));
	assert(a_again->isInstanceRoot());
	assert(not a_again->sourcePrefab().hasValue());
	assert(WorldTestAccess::childrenOf(*a_again).empty());
	}
}
