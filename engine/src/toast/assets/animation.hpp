/**
 * @file animation.hpp
 * @author dario
 *
 * @brief Keyframe animation asset (.tanim), imported from glTF
 *
 * Bump @ref _detail::animation_format_version on any layout change
 */

#pragma once
#include "core_types.hpp"
#include "skeleton.hpp"

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <span>
#include <string>
#include <toast/export.hpp>
#include <unordered_map>
#include <vector>

namespace assets {

namespace _detail {
/// Binary layout version for .tanim; readers reject anything they don't recognise
inline constexpr uint32_t animation_format_version = 5;
}

/// @brief How a sampler interpolates between two keyframes, matching glTF's three modes
enum class Interpolation : uint8_t {
	linear,
	step,
	/// Cubic Hermite. Each keyframe stores three values (in-tangent, value, out-tangent) rather than one,
	/// so a cubic track's value array is 3x as long as its time array
	cubic_spline,
};

/// @brief Which part of a node's transform a track drives
enum class TrackTarget : uint8_t {
	translation,
	rotation,
	scale,
};

/**
 * @brief One animated channel: a keyframe curve driving one component of one node's transform
 *
 * Times are seconds from clip start. Only one of @ref vec3_values / @ref quat_values is populated, chosen
 * by @ref target
 */
struct TOAST_API AnimationTrack {
	std::string target_node;    ///< node name, resolved against the instantiated tree at playback time
	TrackTarget target = TrackTarget::translation;
	Interpolation interpolation = Interpolation::linear;

	std::vector<float> times;
	std::vector<glm::vec3> vec3_values;
	std::vector<glm::quat> quat_values;

	/// @returns keyframe count, accounting for cubic spline storing three values per key
	[[nodiscard]]
	auto keyframeCount() const -> size_t {
		return times.size();
	}

	/**
	 * @brief Evaluates this track at @p time, in seconds from clip start
	 *
	 * Clamps rather than loops - looping is the player's decision and it wraps the time first. A track with
	 * no keys returns identity, so a malformed one poses to something sane rather than to garbage
	 */
	[[nodiscard]]
	auto sampleVec3(float time) const -> glm::vec3;

	[[nodiscard]]
	auto sampleQuat(float time) const -> glm::quat;
};

/// @brief One node's sampled transform at a point in a clip
///
/// Flagged per component: a clip may drive only some, and an unanimated one must keep whatever the node
/// already has rather than snap to identity
struct NodePose {
	alignas(16) glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};

	glm::vec3 translation {0.0f};
	// glm::vec3 scale {1.0f};	FUCK THE SCALE

	// uint8_t has_translation = false;
	// uint8_t has_rotation = false;

	float _pad;
};

/// @brief A named set of tracks that play together
struct TOAST_API AnimationClip {
	std::string name;
	float duration = 0.0f;    ///< seconds; the largest time across all tracks
	std::vector<AnimationTrack> tracks;

	/**
	 * @brief Samples every track at @p time, grouped per target node
	 * @param time Seconds from clip start; not wrapped, so the caller decides whether to loop
	 * @returns one entry per node this clip actually drives, keyed by node name
	 */
	[[nodiscard]]
	auto sample(float time) const -> std::unordered_map<std::string, NodePose>;

	/// Writes tracks whose @p track_to_bone entry is a valid bone into @p pose, no allocation
	void sampleInto(float time, std::span<const int32_t> track_to_bone, std::span<BonePose> pose) const;

	/// @brief Wraps @p time into [0, duration], for looping playback
	[[nodiscard]]
	auto wrap(float time) const -> float {
		if (duration <= 0.0f) {
			return 0.0f;
		}
		const float wrapped = std::fmod(time, duration);
		return wrapped < 0.0f ? wrapped + duration : wrapped;
	}
};

/// One clip asset (.tanim)
class TOAST_API Animation : public Asset {
public:
	explicit Animation(std::span<const uint8_t> data);

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "animation";
	}

	[[nodiscard]]
	auto clip() const noexcept -> const AnimationClip& {
		return m_clip;
	}

	[[nodiscard]]
	static auto toBinary(const AnimationClip& clip) -> std::vector<uint8_t>;

private:
	AnimationClip m_clip;
};

}
