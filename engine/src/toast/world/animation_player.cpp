#include "animation_player.hpp"

namespace toast {

auto resolveClip(const assets::Animation& animation, std::string_view clip_name, std::string_view owner)
    -> const assets::AnimationClip* {
	// The importer writes one clip per .tanim so the Clip field is normally blank
	if (clip_name.empty()) {
		if (animation.clips().empty()) {
			TOAST_WARN("Animation", "'{}': assigned animation asset has no clips", owner);
			return nullptr;
		}
		return &animation.clips().front();
	}
	const assets::AnimationClip* clip = animation.findClip(clip_name);
	if (clip == nullptr) {
		TOAST_WARN("Animation", "'{}': animation asset has no clip named '{}'", owner, clip_name);
	}
	return clip;
}

auto AnimationPlayer::play(std::string_view clip_name) -> bool {
	if (!m_animation.hasValue()) {
		TOAST_WARN("Animation", "AnimationPlayer '{}' has no animation asset assigned", name());
		return false;
	}

	const assets::AnimationClip* clip = resolveClip(*m_animation, clip_name, name());
	if (clip == nullptr) {
		return false;
	}

	// A new clip targets a potentially different set of nodes, so the resolved-node memo is invalid
	if (clip != m_playback.clip) {
		m_node_cache.clear();
	}

	m_playback.start(clip);
	m_clip_name = clip->name;
	return true;
}

auto AnimationPlayer::inspectorOptions(std::string_view field_name) -> std::vector<std::string> {
	std::vector<std::string> options;
	if (field_name == "m_clip_name" && m_animation.hasValue()) {
		for (const auto& clip : m_animation->clips()) {
			options.push_back(clip.name);
		}
	}
	return options;
}

void AnimationPlayer::stop() {
	m_playback.stop();
}

void AnimationPlayer::begin() {
	// No clip-name check: with one clip per asset the field is normally blank, and play() resolves that to
	// the asset's only clip
	if (m_play_on_begin && m_animation.hasValue()) {
		play(m_clip_name);
	}
}

auto AnimationPlayer::resolveNode(const std::string& node_name) -> Node3D* {
	if (const auto it = m_node_cache.find(node_name); it != m_node_cache.end()) {
		// Cached but since destroyed - Box tracks that, so re-resolving is pointless within this frame
		return it->second.exists() ? &*it->second : nullptr;
	}

	Box<Node3D> found;
	for (const auto& child : children()) {
		Box<Node> cursor = child;
		if (!cursor.exists()) {
			continue;
		}
		if (auto match = cursor->find(node_name).as<Node3D>(); match.exists()) {
			found = match;
			break;
		}
	}

	m_node_cache.emplace(node_name, found);
	return found.exists() ? &*found : nullptr;
}

void AnimationPlayer::tick() {
	ZoneScoped;
	if (!m_playback.advance(static_cast<float>(Time::delta()), m_speed, m_loop)) {
		return;
	}

	for (const auto& [node_name, pose] : m_playback.clip->sample(m_playback.time)) {
		Node3D* target = resolveNode(node_name);
		if (target == nullptr) {
			continue;
		}

		target->position = pose.translation;
		target->rotation = pose.rotation;
		target->syncTransform();
	}
}

}
