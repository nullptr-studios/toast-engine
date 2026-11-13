/**
 * @file clip_playback.hpp
 * @author dario
 * @date 9 Nov 2026
 */

#pragma once
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <toast/assets/animation.hpp>
#include <vector>

namespace toast {

/// Clip clock shared by AnimationPlayer and SkeletalNode
struct ClipPlayback {
	/// Held so removing the clip from Clips cannot free it under playback
	assets::Handle<assets::Animation> asset;
	const assets::AnimationClip* clip = nullptr;
	float time = 0.0f;
	bool playing = false;
	bool paused = false;

	void start(const assets::Handle<assets::Animation>& new_asset) {
		asset = new_asset;
		clip = asset.hasValue() ? &asset->clip() : nullptr;
		time = 0.0f;
		playing = clip != nullptr;
		paused = false;
	}

	/// Stops and releases the clip once it is no longer in @p clips
	void dropIfRemoved(std::span<const assets::Handle<assets::Animation>> clips) {
		if (!asset.hasValue() ||
		    std::ranges::any_of(clips, [&](const auto& candidate) { return candidate.hasValue() && &*candidate == &*asset; })) {
			return;
		}
		stop();
		asset = {};
		clip = nullptr;
	}

	void stop() {
		playing = false;
		time = 0.0f;
	}

	/// @returns false when nothing advanced so the pose is unchanged
	auto advance(float delta, float speed, bool loop) -> bool {
		if (!playing || paused || clip == nullptr) {
			return false;
		}
		time += delta * speed;
		if (clip->duration > 0.0f && time >= clip->duration) {
			if (loop) {
				time = clip->wrap(time);
			} else {
				time = clip->duration;
				playing = false;
			}
		}
		return true;
	}
};

/// Empty name means the first loaded clip, logs under @p owner when nothing matches
TOAST_API auto
    resolveClip(std::span<const assets::Handle<assets::Animation>> clips, std::string_view clip_name, std::string_view owner)
        -> const assets::Handle<assets::Animation>*;

/// Names of the loaded clips, for an Autoplay dropdown
TOAST_API auto clipNames(std::span<const assets::Handle<assets::Animation>> clips) -> std::vector<std::string>;

}
