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

using namespace toast;
using namespace assets;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

constexpr const char* SCENE_ASSET = "SceneUnknown";

const std::string PREFAB_WITH_DEAD_TYPE =
    "[scene_root type=toast::Node]\n"
    "m_uid @uid = sceneROOT00\n"
    "\n"
    "[dead_child type=toast::deadType]\n"
    "m_uid @uid = deadCHILD00\n"
    "m_parent @uid = sceneROOT00\n"
    "m_extents @vec3 = 4, 4, 4\n"
    "\n"
    "[live_child type=toast::Node]\n"
    "m_uid @uid = liveCHILD00\n"
    "m_parent @uid = sceneROOT00\n";

auto uidOf(const char* s) -> uint64_t {
	return UID::fromString(s);
}

}    // namespace

// A scene naming a type this build does not have must still load. Release used to read the factory
// pointer straight off a null NodeInfo, which faulted at 0xc8 - NodeInfo::construct's offset - while
// Debug took a fallback the preprocessor kept out of Release entirely
TOAST_TEST_NAMED("world", "world/03-unknown-node-type", test_world_03) {
	std::stringstream ss(PREFAB_WITH_DEAD_TYPE);
	Prefab prefab(ss);
	UID asset_id(UID::fromString(SCENE_ASSET));
	Handle<Prefab> handle(&prefab, asset_id, "");

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;

	Box<Node> root = WorldTestAccess::instantiate(*world, handle, ctx);
	assert(root.exists());
	assert(root->uid().data() == uidOf("sceneROOT00"));

	// The unknown node is substituted, not dropped, so the rest of the tree keeps its shape
	const auto& children = WorldTestAccess::childrenOf(*root);
	assert(children.size() == 2);

	Box<Node> dead;
	Box<Node> live;
	for (const auto& child : children) {
		if (child->uid().data() == uidOf("deadCHILD00")) {
			dead = child;
		} else if (child->uid().data() == uidOf("liveCHILD00")) {
			live = child;
		}
	}
	assert(dead.exists());
	assert(live.exists());

	// Substituted with the base type, and reflection is attached rather than left null - the fields
	// the dead type declared have nowhere to go and are skipped
	assert(dead->info() != nullptr);
	assert(dead->info()->type == "toast::Node");
	assert(dead->name() == "dead child");
}
