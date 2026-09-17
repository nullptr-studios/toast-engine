#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/uid.hpp>
#include <vector>

using namespace toast;
using namespace assets;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

std::vector<uint64_t> g_enabled;

void recordEnable(void* node) {
	g_enabled.push_back(static_cast<Node*>(node)->uid().data());
}

auto nodeChunk(const char* name, const char* uid, const char* parent, bool enabled) -> std::string {
	std::string chunk = std::string("[") + name + " type=toast::Node]\nm_uid @uid = " + uid + "\n";
	if (parent != nullptr) {
		chunk += std::string("m_parent @uid = ") + parent + "\n";
	}
	return chunk + "m_local_enabled @bool = " + (enabled ? "true" : "false") + "\n\n";
}

void hookSubtree(Node& node) {
	WorldTestAccess::setEnableCallback(node, &recordEnable);
	for (Box<Node> child : WorldTestAccess::childrenOf(node)) {
		hookSubtree(*child);
	}
}

auto enableCount(const char* uid) -> std::ptrdiff_t {
	return std::ranges::count(g_enabled, UID::fromString(uid));
}

}    // namespace

// Loading a scene fires onEnable on every enabled node of the tree, not only the root. Instantiation already sets the
// enabled flags, which used to make the activation a no-op, so an AudioEmitter with play on enable stayed silent
TOAST_TEST_NAMED("world", "world/05-enable-on-load", test_world_05) {
	WorldTestAccess::initThreadPool();

	const std::string text = nodeChunk("scene_root", "rootNODE000", nullptr, true) +
	                         nodeChunk("child_a", "childA00000", "rootNODE000", true) +
	                         nodeChunk("grand_a", "grandA00000", "childA00000", true) +
	                         nodeChunk("child_b", "childB00000", "rootNODE000", false) +
	                         nodeChunk("grand_b", "grandB00000", "childB00000", true);
	std::stringstream ss(text);
	Prefab prefab(ss);
	Handle<Prefab> handle(&prefab, UID(UID::fromString("SceneEnable")), "");

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	Box<Node> root = WorldTestAccess::instantiate(*world, handle, ctx);
	assert(root.exists());
	hookSubtree(*root);

	g_enabled.clear();
	WorldTestAccess::activateLoadedRoot(*world, *root);

	assert(enableCount("rootNODE000") == 1);
	assert(enableCount("childA00000") == 1);
	assert(enableCount("grandA00000") == 1);
	// A disabled branch stays silent all the way down
	assert(enableCount("childB00000") == 0);
	assert(enableCount("grandB00000") == 0);
	assert(g_enabled.size() == 3);
}
