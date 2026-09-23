/**
 * @file animation_player.hpp
 * @author dario
 *
 * @brief Plays a clip from a .tanim asset onto the nodes it targets
 */

#pragma once
#include "node_3d.hpp"

#include <string>
#include <toast/assets/animation.hpp>
#include <toast/assets/types.hpp>
#include <unordered_map>

namespace toast {

/**
 * @brief Drives node transforms from an Animation clip
 *
 * Targets are resolved by name against this player's descendants, never itself - so the same clip drives
 * several prefab instances independently
 *
 * Only the components a clip actually animates are written: a node keyed for rotation but not position
 * keeps its authored position rather than snapping to the origin
 *
 * @note Also ticked outside Play, by Workspace::tickAnimationPreviews()
 */
class [[ToastNode, Icon("Film")]] TOAST_API AnimationPlayer : public Node3D {
public:
	AnimationPlayer() = default;

	/// @brief Starts the named clip from the beginning
	/// @returns false when the asset has no clip by that name
	[[Reflect]]
	auto play(std::string_view clip_name) -> bool;

	[[Reflect]]
	void stop();

	[[Reflect]]
	void pause(bool value) {
		m_paused = value;
	}

	[[nodiscard]]
	auto isPlaying() const noexcept -> bool {
		return m_playing;
	}

	/// @returns seconds into the current clip
	[[nodiscard]]
	auto time() const noexcept -> float {
		return m_time;
	}

	void time(float seconds) noexcept { m_time = seconds; }

	/**
	 * @brief World transforms of a skin's joints, in joint-index order
	 *
	 * A joint the tree does not contain contributes an identity rather than shifting every later index
	 */
	[[nodiscard]]
	auto jointWorldTransforms(const assets::Skin& skin) -> std::vector<glm::mat4>;

	/**
	 * @brief Advances playback by one frame and writes the sampled pose onto its target nodes
	 *
	 * Public so Workspace can drive editor previews: it never runs the tick scheduler, so without that
	 * nothing would animate outside Play
	 */
	void tick();

private:
	void begin();

	/// @brief Finds a node by name among this player's descendants, memoised for the lifetime of the clip
	auto resolveNode(const std::string& name) -> Node3D*;

	[[Reflect, Name("Animation")]]
	assets::Handle<assets::Animation> m_animation;

	[[Reflect, Name("Clip")]]
	std::string m_clip_name;

	[[Reflect, Name("Play On Begin")]]
	bool m_play_on_begin = true;

	[[Reflect, Name("Loop")]]
	bool m_loop = true;

	[[Reflect, Unit("x"), Range(0.0, 10.0)]]
	float m_speed = 1.0f;

	bool m_playing = false;
	bool m_paused = false;
	float m_time = 0.0f;

	const assets::AnimationClip* m_active_clip = nullptr;

	// Name lookups walk the subtree, which is far too expensive to redo per track per frame. Cleared
	// whenever the clip changes, since a different clip may target an entirely different set of nodes
	std::unordered_map<std::string, Box<Node3D>> m_node_cache;
};

}

#include <animationplayer.generated.hpp>
