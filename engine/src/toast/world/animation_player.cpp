#include "animation_player.hpp"

namespace toast {

auto resolveClip(std::span<const assets::Handle<assets::Animation>> clips, std::string_view clip_name, std::string_view owner)
    -> const assets::AnimationClip* {
	for (const auto& clip : clips) {
		if (clip.hasValue() && (clip_name.empty() || clip->clip().name == clip_name)) {
			return &clip->clip();
		}
	}
	if (clip_name.empty()) {
		TOAST_WARN("Animation", "'{}' has no clips assigned", owner);
	} else {
		TOAST_WARN("Animation", "'{}' has no clip named '{}'", owner, clip_name);
	}
	return nullptr;
}

auto clipNames(std::span<const assets::Handle<assets::Animation>> clips) -> std::vector<std::string> {
	std::vector<std::string> names;
	names.reserve(clips.size());
	for (const auto& clip : clips) {
		if (clip.hasValue()) {
			names.push_back(clip->clip().name);
		}
	}
	return names;
}

auto AnimationPlayer::play(std::string_view clip_name) -> bool {
	const assets::AnimationClip* clip = resolveClip(m_clips, clip_name, name());
	if (clip == nullptr) {
		return false;
	}

	// A new clip targets a potentially different set of nodes, so the resolved-node memo is invalid
	if (clip != m_playback.clip) {
		m_node_cache.clear();
	}
	m_playback.start(clip);
	return true;
}

auto AnimationPlayer::inspectorOptions(std::string_view field_name) -> std::vector<std::string> {
	return field_name == "m_autoplay" ? clipNames(m_clips) : std::vector<std::string> {};
}

void AnimationPlayer::stop() {
	m_playback.stop();
}

void AnimationPlayer::begin() {
	if (!m_autoplay.empty()) {
		play(m_autoplay);
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
