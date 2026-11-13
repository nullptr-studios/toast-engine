/**
 * @file skeletal_node.hpp
 * @author dario
 * @date 9 Nov 2026
 */

#pragma once
#include "clip_playback.hpp"
#include "node_3d.hpp"

#include <span>
#include <string>
#include <toast/assets/animation.hpp>
#include <toast/assets/types.hpp>
#include <unordered_map>
#include <vector>

namespace toast {

/// Also ticked outside Play by Workspace::tickAnimationPreviews()
class [[ToastNode, Icon("Bone")]] TOAST_API SkeletalNode : public Node3D {
public:
	SkeletalNode() = default;

	/// Empty plays the first clip
	[[Reflect]]
	auto play(std::string_view clip_name) -> bool;

	[[Reflect]]
	void stop();

	[[Reflect]]
	void pause(bool value) {
		m_playback.paused = value;
	}

	[[nodiscard]]
	auto isPlaying() const noexcept -> bool {
		return m_playback.playing;
	}

	[[nodiscard]]
	auto time() const noexcept -> float {
		return m_playback.time;
	}

	void time(float seconds) noexcept { m_playback.time = seconds; }

	[[nodiscard, Reflect]]
	auto boneIndex(std::string_view bone_name) -> int32_t;

	[[nodiscard]]
	auto boneCount() const noexcept -> int32_t {
		return static_cast<int32_t>(m_model.size());
	}

	[[nodiscard]]
	auto boneName(int32_t bone) const -> std::string_view;

	[[nodiscard]]
	auto boneModelTransform(int32_t bone) const -> const glm::mat4&;

	[[nodiscard]]
	auto boneWorldTransform(int32_t bone) -> glm::mat4;

	[[nodiscard]]
	auto boneParents() const noexcept -> std::span<const int32_t> {
		return m_parents;
	}

	[[nodiscard]]
	auto skeleton() -> const assets::Skeleton*;

	/// Skin joint index to bone index, -1 where this skeleton lacks the joint
	[[nodiscard]]
	auto skinBones(const assets::Skin& skin) -> std::span<const int32_t>;

	/// Advances playback, poses bones and moves attachments, public for editor previews
	void tick();

	void drawBones();

	[[nodiscard]]
	auto alwaysDrawsBones() const noexcept -> bool {
		return m_draw_bones;
	}

private:
	void init();
	void destroy();
	void begin();
	void updateInspectorMessages() override;
	auto inspectorOptions(std::string_view field_name) -> std::vector<std::string> override;

	auto bindSkeleton() -> const assets::Skeleton*;
	void bindTracks(const assets::Skeleton& skeleton);
	void computeModel(const assets::Skeleton& skeleton);
	void applyNodeTracks();
	void applyAttachments(const assets::Skeleton& skeleton);
	auto resolveNode(const std::string& name) -> Node3D*;

	[[Reflect, Name("Skeleton")]]
	assets::Handle<assets::Skeleton> m_skeleton;

	/// Bone name keyed, any clip plays on any skeleton with matching names
	[[Reflect, Name("Clips")]]
	std::vector<assets::Handle<assets::Animation>> m_clips;

	/// Played on begin, empty plays nothing
	[[Reflect, Name("Autoplay"), Options]]
	std::string m_autoplay;

	[[Reflect, Name("Loop")]]
	bool m_loop = true;

	[[Reflect, Unit("x"), Range(0.0, 10.0)]]
	float m_speed = 1.0f;

	/// Off still draws while selected in the editor
	[[Reflect, Name("Draw Bones"), Group("Debug")]]
	bool m_draw_bones = false;

	ClipPlayback m_playback;

	const assets::Skeleton* m_bound = nullptr;
	std::vector<assets::BonePose> m_local;
	std::vector<glm::mat4> m_model;
	std::vector<int32_t> m_parents;

	/// Per track of m_track_clip, -1 for tracks that target nodes
	std::vector<int32_t> m_track_to_bone;
	const assets::AnimationClip* m_track_clip = nullptr;

	std::unordered_map<const assets::Skin*, std::vector<int32_t>> m_skin_bones;
	std::unordered_map<std::string, Box<Node3D>> m_node_cache;
};

}
