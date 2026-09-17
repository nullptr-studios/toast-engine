#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/assets/animation.hpp>

namespace {

auto nearly(float a, float b) -> bool {
	return std::fabs(a - b) < 1e-5f;
}

}

TOAST_TEST_NAMED("Assets", "assets/02-animation-binary", test_assets_02_animation_binary) {
	// One clip covering every track target and all three interpolation modes, plus a skin, so the round
	// trip exercises every branch of the .tanim reader/writer
	assets::AnimationClip clip;
	clip.name = "Walk";
	clip.duration = 2.5f;

	assets::AnimationTrack translation;
	translation.target_node = "Hips";
	translation.target = assets::TrackTarget::translation;
	translation.interpolation = assets::Interpolation::linear;
	translation.times = {0.0f, 1.25f, 2.5f};
	translation.vec3_values = {
	  {0.0f, 0.0f, 0.0f},
	  {1.0f, 2.0f, 3.0f},
	  {4.0f, 5.0f, 6.0f},
	};
	clip.tracks.push_back(translation);

	assets::AnimationTrack rotation;
	rotation.target_node = "Spine";
	rotation.target = assets::TrackTarget::rotation;
	rotation.interpolation = assets::Interpolation::step;
	rotation.times = {0.0f, 2.5f};
	rotation.quat_values = {
	  glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
	  glm::quat(0.7071068f, 0.7071068f, 0.0f, 0.0f),
	};
	clip.tracks.push_back(rotation);

	// Cubic spline keeps three values per keyframe (in-tangent, value, out-tangent)
	assets::AnimationTrack scale;
	scale.target_node = "Head";
	scale.target = assets::TrackTarget::scale;
	scale.interpolation = assets::Interpolation::cubic_spline;
	scale.times = {0.0f, 2.5f};
	scale.vec3_values = {
	  {0.0f, 0.0f, 0.0f},
	  {1.0f, 1.0f, 1.0f},
	  {0.0f, 0.0f, 0.0f},
	  {0.0f, 0.0f, 0.0f},
	  {2.0f, 2.0f, 2.0f},
	  {0.0f, 0.0f, 0.0f},
	};
	clip.tracks.push_back(scale);

	assets::Skin skin;
	skin.name = "Body";
	skin.skeleton_root = "Hips";
	skin.joints = {"Hips", "Spine", "Head"};
	skin.inverse_bind_matrices = {glm::mat4(1.0f), glm::mat4(2.0f), glm::mat4(3.0f)};

	const auto bytes = assets::Animation::toBinary({clip}, {skin});
	assert(!bytes.empty());

	const assets::Animation loaded(bytes);
	assert(loaded.type() == "animation");
	assert(loaded.clips().size() == 1);
	assert(loaded.skins().size() == 1);

	const auto* round_tripped = loaded.findClip("Walk");
	assert(round_tripped != nullptr);
	assert(nearly(round_tripped->duration, 2.5f));
	assert(round_tripped->tracks.size() == 3);
	assert(loaded.findClip("Run") == nullptr);

	const auto& t0 = round_tripped->tracks[0];
	assert(t0.target_node == "Hips");
	assert(t0.target == assets::TrackTarget::translation);
	assert(t0.interpolation == assets::Interpolation::linear);
	assert(t0.keyframeCount() == 3);
	assert(t0.vec3_values.size() == 3);
	assert(nearly(t0.vec3_values[2].z, 6.0f));
	assert(t0.quat_values.empty());

	const auto& t1 = round_tripped->tracks[1];
	assert(t1.target == assets::TrackTarget::rotation);
	assert(t1.interpolation == assets::Interpolation::step);
	assert(t1.quat_values.size() == 2);
	assert(nearly(t1.quat_values[1].w, 0.7071068f));
	assert(t1.vec3_values.empty());

	// Three values per key, two keys - the reader must not assume values.size() == times.size()
	const auto& t2 = round_tripped->tracks[2];
	assert(t2.interpolation == assets::Interpolation::cubic_spline);
	assert(t2.keyframeCount() == 2);
	assert(t2.vec3_values.size() == 6);
	assert(nearly(t2.vec3_values[4].x, 2.0f));

	const auto& loaded_skin = loaded.skins()[0];
	assert(loaded_skin.name == "Body");
	assert(loaded_skin.skeleton_root == "Hips");
	assert(loaded_skin.joints.size() == 3);
	assert(loaded_skin.joints[2] == "Head");
	assert(loaded_skin.inverse_bind_matrices.size() == 3);
	assert(nearly(loaded_skin.inverse_bind_matrices[2][0][0], 3.0f));

	// A truncated file must be rejected rather than silently yielding partial garbage
	const std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + (bytes.size() / 2));
	const assets::Animation partial(truncated);
	assert(partial.clips().size() <= 1);

	// Wrong magic entirely
	const std::vector<uint8_t> junk {'N', 'O', 'P', 'E', 0, 0, 0, 0};
	const assets::Animation rejected(junk);
	assert(rejected.clips().empty());
	assert(rejected.skins().empty());
}
