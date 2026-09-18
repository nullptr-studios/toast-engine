/**
 * @file animation.hpp
 * @author dario
 *
 * @brief Keyframe animation asset (.tanim), imported from glTF
 *
 * One clip plus the skins it poses, one file per animation the glTF defines. Sampled by
 * toast::AnimationPlayer, GPU-skinned by renderer::SkinningPass
 *
 * Bump @ref _detail::animation_format_version on any layout change
 */

#pragma once
#include "core_types.hpp"

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
inline constexpr uint32_t animation_format_version = 3;
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
	glm::vec3 translation {0.0f};
	glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 scale {1.0f};
	bool has_translation = false;
	bool has_rotation = false;
	bool has_scale = false;
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

/**
 * @brief A skeleton: the joint nodes a skinned mesh binds to
 *
 * jointMatrices() turns a posed node tree into the matrices a shader blends between. A mesh whose joints
 * cannot be resolved gets no posed slice and falls back to bind pose rather than dropping
 */
struct TOAST_API Skin {
	std::string name;
	std::string skeleton_root;                       ///< node name, empty when glTF didn't specify one
	std::vector<std::string> joints;                 ///< node names, in the order joint indices refer to
	std::vector<glm::mat4> inverse_bind_matrices;    ///< parallel to joints; empty means identity for all

	/**
	 * @brief Builds the per-joint matrices a skinning shader multiplies vertices by
	 *
	 * `joint_world_transform * inverse_bind_matrix`, so the product expresses only deformation away from
	 * bind - which is what the vertex weights blend between
	 *
	 * @param joint_world_transforms World transforms of @ref joints, same order. A missing entry falls back
	 *        to identity rather than being skipped, so the result always matches joints.size() and a shader
	 *        can index it without bounds checks
	 *
	 * @note Supplied by the caller rather than walked here, so several instances can pose one skeleton asset
	 */
	[[nodiscard]]
	auto jointMatrices(std::span<const glm::mat4> joint_world_transforms) const -> std::vector<glm::mat4>;
};

/**
 * @class Animation
 * @brief A set of clips plus the skins they pose
 *
 * The format holds many clips, but the importer writes one per file so each is its own asset in the browser.
 * Every file carries the skins too, so any one can pose the skeleton alone - and a glTF with skins but no
 * animations still produces a clip-less file for MeshNode::skin_animation to point at
 */
class TOAST_API Animation : public Asset {
public:
	/// @brief Parses the binary .tanim representation
	explicit Animation(std::span<const uint8_t> data);

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "animation";
	}

	[[nodiscard]]
	auto clips() const noexcept -> const std::vector<AnimationClip>& {
		return m_clips;
	}

	[[nodiscard]]
	auto skins() const noexcept -> const std::vector<Skin>& {
		return m_skins;
	}

	/// @returns the clip with this name, or nullptr
	[[nodiscard]]
	auto findClip(std::string_view name) const -> const AnimationClip*;

	/// @brief Serialises clips + skins into the binary .tanim layout the constructor reads back
	[[nodiscard]]
	static auto toBinary(const std::vector<AnimationClip>& clips, const std::vector<Skin>& skins) -> std::vector<uint8_t>;

private:
	std::vector<AnimationClip> m_clips;
	std::vector<Skin> m_skins;
};

}
