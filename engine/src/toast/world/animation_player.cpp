#include "animation_player.hpp"

#include "mesh_node.hpp"

#include <toast/log.hpp>
#include <toast/time.hpp>
#include <tracy/Tracy.hpp>

namespace toast {

auto AnimationPlayer::play(std::string_view clip_name) -> bool {
	if (!m_animation.hasValue()) {
		TOAST_WARN("Animation", "AnimationPlayer '{}' has no animation asset assigned", name());
		return false;
	}

	// An empty name means "whatever this asset holds". The glTF importer writes one clip per .tanim, so the
	// Clip field is normally left blank in the inspector and swapping animations is a matter of assigning a
	// different asset - naming the clip is only needed for a hand-authored multi-clip file
	const assets::AnimationClip* clip = nullptr;
	if (clip_name.empty()) {
		if (!m_animation->clips().empty()) {
			clip = &m_animation->clips().front();
		}
	} else {
		clip = m_animation->findClip(clip_name);
	}
	if (clip == nullptr) {
		if (clip_name.empty()) {
			TOAST_WARN("Animation", "AnimationPlayer '{}': assigned animation asset has no clips", name());
		} else {
			TOAST_WARN("Animation", "Animation asset has no clip named '{}'", clip_name);
		}
		return false;
	}

	// A new clip targets a potentially different set of nodes, so the resolved-node memo is invalid
	if (clip != m_active_clip) {
		m_node_cache.clear();
	}

	m_active_clip = clip;
	// Resolved name, not the requested one: an empty request has to leave the inspector showing what is
	// actually playing rather than staying blank
	m_clip_name = clip->name;
	m_time = 0.0f;
	m_playing = true;
	m_paused = false;
	return true;
}

void AnimationPlayer::stop() {
	m_playing = false;
	m_time = 0.0f;
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

	// Descendants only, deliberately never this player itself. Node::find() searches from its origin
	// inclusive, and the importer wraps a whole glTF scene in an AnimationPlayer named after that scene -
	// which for a single-root file is the very name that root node also carries. Searching from `this`
	// matches the wrapper and stops, so a transform track would pose the wrapper instead of the real node. A
	// clip target always names an authored node, never the engine-inserted player, so skipping self is the
	// correct rule in general rather than a workaround for that one collision
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

	// Cached even when the lookup failed (an empty Box), so a clip targeting a node this tree doesn't have
	// costs one subtree walk rather than one per frame forever
	m_node_cache.emplace(node_name, found);
	return found.exists() ? &*found : nullptr;
}

void AnimationPlayer::tick() {
	ZoneScoped;
	if (!m_playing || m_paused || m_active_clip == nullptr) {
		return;
	}

	m_time += static_cast<float>(Time::delta()) * m_speed;

	if (m_active_clip->duration > 0.0f && m_time >= m_active_clip->duration) {
		if (m_loop) {
			m_time = m_active_clip->wrap(m_time);
		} else {
			// Hold the final pose rather than snapping back to bind, which is what a one-shot should leave
			// behind
			m_time = m_active_clip->duration;
			m_playing = false;
		}
	}

	for (const auto& [node_name, pose] : m_active_clip->sample(m_time)) {
		Node3D* target = resolveNode(node_name);
		if (target == nullptr) {
			continue;
		}

		// Only the components the clip actually keys - see NodePose
		if (pose.has_translation) {
			target->position = pose.translation;
		}
		if (pose.has_rotation) {
			target->rotation = pose.rotation;
		}
		if (pose.has_scale) {
			target->scale = pose.scale;
		}
		target->syncTransform();
	}
}

auto AnimationPlayer::jointWorldTransforms(const assets::Skin& skin) -> std::vector<glm::mat4> {
	std::vector<glm::mat4> transforms(skin.joints.size(), glm::mat4(1.0f));

	for (size_t i = 0; i < skin.joints.size(); ++i) {
		Node3D* joint = resolveNode(skin.joints[i]);
		if (joint == nullptr) {
			continue;    // leaves identity, keeping later joint indices aligned
		}
		joint->syncTransform();
		transforms[i] = joint->getWorldTransform();
	}

	return transforms;
}

}
