#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/assets/animation.hpp>

namespace {

auto nearly(float a, float b) -> bool {
	return std::fabs(a - b) < 1e-5f;
}

}

// The glTF importer writes one .tanim per clip, and a glTF that defines a skeleton but no animation still
// needs a file for MeshNode::skin_animation to resolve against - a clip-less .tanim. Nothing else in the
// engine produces one, so the reader's zero-clip path only exists for this case
TOAST_TEST_NAMED("Assets", "assets/04-animation-skin-only", test_assets_04_animation_skin_only) {
	assets::Skin skin;
	skin.name = "Body";
	skin.skeleton_root = "Hips";
	skin.joints = {"Hips", "Spine", "Head"};
	skin.inverse_bind_matrices = {glm::mat4(1.0f), glm::mat4(2.0f), glm::mat4(3.0f)};

	const auto bytes = assets::Animation::toBinary({}, {skin});
	assert(!bytes.empty());

	const assets::Animation loaded(bytes);
	assert(loaded.type() == "animation");
	assert(loaded.clips().empty());
	assert(loaded.skins().size() == 1);

	// Asking for any clip has to come back empty-handed rather than reaching into an empty vector -
	// AnimationPlayer::play() calls this before it has any way of knowing the asset is clip-less
	assert(loaded.findClip("Walk") == nullptr);
	assert(loaded.findClip("") == nullptr);

	const auto& loaded_skin = loaded.skins()[0];
	assert(loaded_skin.name == "Body");
	assert(loaded_skin.skeleton_root == "Hips");
	assert(loaded_skin.joints.size() == 3);
	assert(loaded_skin.joints[2] == "Head");
	assert(loaded_skin.inverse_bind_matrices.size() == 3);
	assert(nearly(loaded_skin.inverse_bind_matrices[1][0][0], 2.0f));

	// The skin still poses without any clip driving it: joint matrices are joint_world * inverse_bind, so a
	// skeleton-only asset is enough to draw a skinned mesh in bind pose
	const std::vector<glm::mat4> world_transforms(3, glm::mat4(1.0f));
	const auto matrices = loaded_skin.jointMatrices(world_transforms);
	assert(matrices.size() == 3);
	assert(nearly(matrices[1][0][0], 2.0f));
}
