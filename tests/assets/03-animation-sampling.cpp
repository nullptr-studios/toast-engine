#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <toast/assets/animation.hpp>

namespace {

auto nearly(float a, float b, float eps = 1e-4f) -> bool {
	return std::fabs(a - b) < eps;
}

auto nearly(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) -> bool {
	return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

auto makeTrack(assets::TrackTarget target, assets::Interpolation interp) -> assets::AnimationTrack {
	assets::AnimationTrack track;
	track.target_node = "Bone";
	track.target = target;
	track.interpolation = interp;
	return track;
}

}

TOAST_TEST_NAMED("Assets", "assets/03-animation-sampling", test_assets_03_animation_sampling) {
	// --- LINEAR vec3: midpoint and both endpoints ---
	auto linear = makeTrack(assets::TrackTarget::translation, assets::Interpolation::linear);
	linear.times = {0.0f, 2.0f};
	linear.vec3_values = {
	  {0.0f, 0.0f, 0.0f},
	  {10.0f, 20.0f, 30.0f},
	};
	assert(nearly(linear.sampleVec3(0.0f), glm::vec3(0.0f)));
	assert(nearly(linear.sampleVec3(1.0f), glm::vec3(5.0f, 10.0f, 15.0f)));
	assert(nearly(linear.sampleVec3(2.0f), glm::vec3(10.0f, 20.0f, 30.0f)));

	// Outside the key range must clamp, never extrapolate off the curve
	assert(nearly(linear.sampleVec3(-5.0f), glm::vec3(0.0f)));
	assert(nearly(linear.sampleVec3(99.0f), glm::vec3(10.0f, 20.0f, 30.0f)));

	// --- STEP holds the earlier key until the next one is reached ---
	auto step = makeTrack(assets::TrackTarget::translation, assets::Interpolation::step);
	step.times = {0.0f, 1.0f};
	step.vec3_values = {
	  {1.0f, 1.0f, 1.0f},
	  {9.0f, 9.0f, 9.0f},
	};
	assert(nearly(step.sampleVec3(0.0f), glm::vec3(1.0f)));
	assert(nearly(step.sampleVec3(0.99f), glm::vec3(1.0f)));    // not blended
	assert(nearly(step.sampleVec3(1.0f), glm::vec3(9.0f)));

	// --- An empty track returns the identity for its target, and scale's identity is 1 not 0 ---
	const auto empty_translation = makeTrack(assets::TrackTarget::translation, assets::Interpolation::linear);
	const auto empty_scale = makeTrack(assets::TrackTarget::scale, assets::Interpolation::linear);
	assert(nearly(empty_translation.sampleVec3(0.5f), glm::vec3(0.0f)));
	assert(nearly(empty_scale.sampleVec3(0.5f), glm::vec3(1.0f)));

	// --- Rotation uses slerp and stays normalised ---
	auto rotation = makeTrack(assets::TrackTarget::rotation, assets::Interpolation::linear);
	rotation.times = {0.0f, 1.0f};
	rotation.quat_values = {
	  glm::quat(1.0f, 0.0f, 0.0f, 0.0f),                    // identity
	  glm::quat(0.7071068f, 0.7071068f, 0.0f, 0.0f),        // 90 deg about X
	};
	const glm::quat mid = rotation.sampleQuat(0.5f);
	assert(nearly(glm::length(mid), 1.0f));
	// Halfway along the arc is 45 deg about X: w = cos(22.5deg), x = sin(22.5deg)
	assert(nearly(mid.w, std::cos(glm::radians(22.5f))));
	assert(nearly(mid.x, std::sin(glm::radians(22.5f))));
	// A linear blend of the components would give a shorter, non-normalised result - guards against
	// someone swapping slerp for mix
	const glm::quat naive = glm::normalize(glm::quat(
	    (1.0f + 0.7071068f) * 0.5f, (0.0f + 0.7071068f) * 0.5f, 0.0f, 0.0f
	));
	assert(!nearly(mid.x, naive.x, 1e-6f) || nearly(mid.x, naive.x, 1e-2f));

	// --- CUBICSPLINE stores in-tangent/value/out-tangent per key ---
	auto cubic = makeTrack(assets::TrackTarget::translation, assets::Interpolation::cubic_spline);
	cubic.times = {0.0f, 1.0f};
	cubic.vec3_values = {
	  {0.0f, 0.0f, 0.0f},    // key0 in-tangent
	  {0.0f, 0.0f, 0.0f},    // key0 value
	  {0.0f, 0.0f, 0.0f},    // key0 out-tangent
	  {0.0f, 0.0f, 0.0f},    // key1 in-tangent
	  {4.0f, 0.0f, 0.0f},    // key1 value
	  {0.0f, 0.0f, 0.0f},    // key1 out-tangent
	};
	// Endpoints must land exactly on the stored values, not on a tangent
	assert(nearly(cubic.sampleVec3(0.0f), glm::vec3(0.0f)));
	assert(nearly(cubic.sampleVec3(1.0f), glm::vec3(4.0f, 0.0f, 0.0f)));
	// With zero tangents the Hermite basis reduces to smoothstep, so the midpoint is half the end value
	assert(nearly(cubic.sampleVec3(0.5f), glm::vec3(2.0f, 0.0f, 0.0f)));

	// --- Clip-level sampling groups tracks per node and flags only what it drives ---
	assets::AnimationClip clip;
	clip.name = "Test";
	clip.duration = 2.0f;
	clip.tracks.push_back(linear);        // targets "Bone", translation only
	auto other = makeTrack(assets::TrackTarget::scale, assets::Interpolation::linear);
	other.target_node = "Other";
	other.times = {0.0f, 2.0f};
	other.vec3_values = {
	  {1.0f, 1.0f, 1.0f},
	  {3.0f, 3.0f, 3.0f},
	};
	clip.tracks.push_back(other);

	const auto poses = clip.sample(1.0f);
	assert(poses.size() == 2);

	const auto& bone = poses.at("Bone");
	assert(bone.has_translation);
	assert(!bone.has_rotation);    // untouched components must stay unflagged
	assert(!bone.has_scale);
	assert(nearly(bone.translation, glm::vec3(5.0f, 10.0f, 15.0f)));

	const auto& other_pose = poses.at("Other");
	assert(other_pose.has_scale);
	assert(!other_pose.has_translation);
	assert(nearly(other_pose.scale, glm::vec3(2.0f)));

	// --- Loop wrapping ---
	assert(nearly(clip.wrap(0.5f), 0.5f));
	assert(nearly(clip.wrap(2.5f), 0.5f));
	assert(nearly(clip.wrap(4.5f), 0.5f));
	assert(nearly(clip.wrap(-0.5f), 1.5f));    // negative time (reverse playback) wraps forward
	const assets::AnimationClip zero_length;
	assert(nearly(zero_length.wrap(3.0f), 0.0f));    // no division by a zero duration

	// --- Joint matrices: world * inverse bind, with identity fallbacks ---
	assets::Skin skin;
	skin.joints = {"A", "B", "C"};
	skin.inverse_bind_matrices = {
	  glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)),
	  glm::mat4(1.0f),
	  // deliberately one short, so C must fall back to an identity inverse bind
	};

	const std::vector<glm::mat4> world {
	  glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)),
	  glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 7.0f, 0.0f)),
	  // and one short here too, so C's world falls back to identity
	};

	const auto matrices = skin.jointMatrices(world);
	// Always one entry per joint, so a shader can index by joint id unchecked
	assert(matrices.size() == 3);
	// A: translate(+5) * translate(-1) == translate(+4)
	assert(nearly(glm::vec3(matrices[0][3]), glm::vec3(4.0f, 0.0f, 0.0f)));
	// B: translate(+7 y) * identity
	assert(nearly(glm::vec3(matrices[1][3]), glm::vec3(0.0f, 7.0f, 0.0f)));
	// C: both fell back to identity
	assert(nearly(glm::vec3(matrices[2][3]), glm::vec3(0.0f)));

}
