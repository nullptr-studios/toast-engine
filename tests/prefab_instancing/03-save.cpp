#include "prefab_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <memory>

using namespace toast;
using namespace toast::tests;
using assets::Prefab;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

constexpr const char* A_ASSET = "AssetA00000";
constexpr const char* B_ASSET = "BBBBBBBBBBB";
constexpr const char* SCENE_VARIANT = "SceneV00000";

const std::string PREFAB_B =
    "[b_root type=toast::Node]\n"
    "m_uid @uid = bROOTnode00\n"
    "\n"
    "[b_child type=toast::Node]\n"
    "m_uid @uid = bCHILDnode0\n"
    "m_parent @uid = bROOTnode00\n";

// A scene whose root contains one instance of B.
const std::string PREFAB_A =
    "[a_root type=toast::Node]\n"
    "m_uid @uid = aROOTnode00\n"
    "\n"
    "[b_instance type=toast::Node]\n"
    "m_uid @uid = PLACEMENT00\n"
    "m_parent @uid = aROOTnode00\n"
    "m_source_prefab @uid = BBBBBBBBBBB\n";

auto chunkByName(const Prefab& p, std::string_view name) -> const Prefab::BasicNode* {
	for (const auto& n : p.nodes) {
		if (n.name == name) {
			return &n;
		}
	}
	return nullptr;
}

}    // namespace

TOAST_TEST_NAMED("prefab_instancing", "prefab_instancing/03-save", test_prefab_instancing_03_save) {
	PrefabStore store;
	store.add(B_ASSET, PREFAB_B);
	store.add(A_ASSET, PREFAB_A);

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = store.resolver();

	Box<Node> root = WorldTestAccess::instantiate(*world, store.handle(A_ASSET), ctx);
	assert(root.exists());

	// --- Save as a definition of A: the instance is written as a reference, NOT inlined ---------
	{
		Prefab saved(*root, UID(uidOf(A_ASSET)));

		// Two chunks only: the definition root and the instance reference. B's interior (b_child)
		// is NOT serialized — it lives in prefab B.
		assert(saved.nodes.size() == 2);

		const auto* def = chunkByName(saved, "A Root");
		const auto* ref = chunkByName(saved, "B Instance");
		assert(def != nullptr);
		assert(ref != nullptr);

		// The definition root carries no Prefab field (it is the asset being written) and no Parent.
		assert(not def->find("m_source_prefab").has_value());
		assert(not def->find("m_parent").has_value());
		assert(def->find("m_uid")->as<UID>().data() == uidOf("aROOTnode00"));

		// The reference chunk carries identity + structure + source prefab, and does not inline B.
		assert(ref->find("m_uid")->as<UID>().data() == uidOf("PLACEMENT00"));
		assert(ref->find("m_parent")->as<UID>().data() == uidOf("aROOTnode00"));
		assert(ref->find("m_source_prefab").has_value());
		assert(ref->find("m_source_prefab")->as<UID>().data() == uidOf(B_ASSET));
		assert(chunkByName(saved, "b_child") == nullptr);

		// --- Round-trip: re-instantiating the saved prefab rebuilds the same shape --------------
		auto world2 = WorldTestAccess::createWorld();
		INodeOwner::InstantiateContext ctx2;
		ctx2.resolver = store.resolver();
		assets::AssetHandle<Prefab> saved_handle(&saved, UID(uidOf(A_ASSET)));

		Box<Node> reloaded = WorldTestAccess::instantiate(*world2, saved_handle, ctx2);
		assert(reloaded.exists());
		assert(reloaded->uid().data() == uidOf("aROOTnode00"));

		const auto& kids = WorldTestAccess::childrenOf(*reloaded);
		assert(kids.size() == 1);
		assert(kids[0]->uid().data() == uidOf("PLACEMENT00"));
		assert(kids[0]->isInstanceRoot());
		assert(kids[0]->sourcePrefab().uid().data() == uidOf(B_ASSET));
		// B's interior is grafted back in from prefab B.
		assert(WorldTestAccess::childrenOf(*kids[0]).size() == 1);
		assert(WorldTestAccess::childrenOf(*kids[0])[0]->uid().data() == uidOf("bCHILDnode0"));
	}

	// --- Save as a VARIANT (different self_uid): the root becomes a reference to A, no recursion --
	{
		Prefab variant(*root, UID(uidOf(SCENE_VARIANT)));

		// Only the variant root is written: a reference to A. Its children belong to A's definition.
		assert(variant.nodes.size() == 1);
		const auto& vroot = variant.nodes[0];
		assert(vroot.name == "A Root");
		assert(vroot.find("m_source_prefab").has_value());
		assert(vroot.find("m_source_prefab")->as<UID>().data() == uidOf(A_ASSET));
		assert(vroot.find("m_uid")->as<UID>().data() == uidOf("aROOTnode00"));
		assert(chunkByName(variant, "B Instance") == nullptr);
	}

	// --- Plain definition (no instances): no chunk carries a Prefab field, interior recursed ----
	{
		auto worldB = WorldTestAccess::createWorld();
		INodeOwner::InstantiateContext ctxB;
		ctxB.resolver = store.resolver();
		Box<Node> b_root = WorldTestAccess::instantiate(*worldB, store.handle(B_ASSET), ctxB);
		assert(b_root.exists());

		Prefab saved_b(*b_root, UID(uidOf(B_ASSET)));
		assert(saved_b.nodes.size() == 2);    // b_root + b_child, fully inlined
		for (const auto& chunk : saved_b.nodes) {
			assert(not chunk.find("m_source_prefab").has_value());
		}
		assert(chunkByName(saved_b, "B Child") != nullptr);
	}
}
