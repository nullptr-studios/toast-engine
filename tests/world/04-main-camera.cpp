#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/uid.hpp>
#include <toast/world/camera.hpp>

using namespace toast;
using namespace assets;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

constexpr const char* SCENE_ASSET = "SceneCamera";

auto uidOf(const char* s) -> uint64_t {
	return UID::fromString(s);
}

auto cameraChunk(const char* name, const char* uid, bool main) -> std::string {
	std::string chunk = std::string("[") + name + " type=toast::Camera]\n" + "m_uid @uid = " + uid + "\n" +
	                    "m_parent @uid = sceneROOT00\n" + "m_local_enabled @bool = true\n";
	if (main) {
		chunk += "m_is_main_camera @bool = true\n";
	}
	return chunk + "\n";
}

auto scene(const std::string& cameras) -> std::string {
	return "[scene_root type=toast::Node]\nm_uid @uid = sceneROOT00\nm_local_enabled @bool = true\n\n" + cameras;
}

// Loads the scene text as the world root and returns the uid of the camera the world renders from,
// or 0 when only the fallback camera is left
auto activeCameraAfterLoad(const std::string& text) -> uint64_t {
	std::stringstream ss(text);
	Prefab prefab(ss);
	Handle<Prefab> handle(&prefab, UID(UID::fromString(SCENE_ASSET)), "");

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext ctx;
	Box<Node> root = WorldTestAccess::instantiate(*world, handle, ctx);
	assert(root.exists());

	WorldTestAccess::activateLoadedRoot(*world, *root);
	if (!WorldTestAccess::hasActiveCamera(*world)) {
		return 0;
	}

	Camera* active = WorldTestAccess::activeRenderCamera(*world);
	assert(active != nullptr);
	return active->uid().data();
}

}    // namespace

// A camera flagged "Is Main Camera?" takes the active slot when its scene loads, whether it begins
// before or after the other cameras; unflagged scenes keep the first-camera-wins behaviour
TOAST_TEST_NAMED("world", "world/04-main-camera", test_world_04) {
	WorldTestAccess::initThreadPool();

	// Flag is parsed from scene text onto the node
	{
		std::stringstream ss(scene(cameraChunk("main_cam", "camMAIN0000", true)));
		Prefab prefab(ss);
		Handle<Prefab> handle(&prefab, UID(UID::fromString(SCENE_ASSET)), "");
		auto world = WorldTestAccess::createWorld();
		INodeOwner::InstantiateContext ctx;
		Box<Node> root = WorldTestAccess::instantiate(*world, handle, ctx);
		assert(root.exists());
		const auto& children = WorldTestAccess::childrenOf(*root);
		assert(children.size() == 1);
		Box<Camera> camera = children.front().as<Camera>();
		assert(camera.exists());
		assert(camera->isMainCamera());
	}

	// Main camera begins after an ordinary one: it must steal the active slot
	assert(
	    activeCameraAfterLoad(scene(cameraChunk("other_cam", "camOTHER000", false) + cameraChunk("main_cam", "camMAIN0000", true))) ==
	    uidOf("camMAIN0000")
	);

	// Main camera begins first: the ordinary one must not take over
	assert(
	    activeCameraAfterLoad(scene(cameraChunk("main_cam", "camMAIN0000", true) + cameraChunk("other_cam", "camOTHER000", false))) ==
	    uidOf("camMAIN0000")
	);

	// No flag anywhere: some camera still becomes active
	const uint64_t unflagged =
	    activeCameraAfterLoad(scene(cameraChunk("cam_a", "camAAAAAAAA", false) + cameraChunk("cam_b", "camBBBBBBBB", false)));
	assert(unflagged == uidOf("camAAAAAAAA") || unflagged == uidOf("camBBBBBBBB"));
}
