#include "skeletal_node.hpp"

#include "bone_attachment.hpp"

#include <glm/gtx/matrix_decompose.hpp>
#include <toast/renderer/debug/debug_draw.hpp>
#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

namespace {

const glm::mat4 k_identity {1.0f};

auto poseMatrix(const assets::BonePose& pose) -> glm::mat4 {
	return glm::translate(glm::mat4(1.0f), pose.translation) * glm::mat4_cast(pose.rotation) *
	       glm::scale(glm::mat4(1.0f), pose.scale);
}

}

auto SkeletalNode::play(std::string_view clip_name) -> bool {
	const assets::AnimationClip* clip = resolveClip(m_clips, clip_name, name());
	if (clip == nullptr) {
		return false;
	}

	if (clip != m_playback.clip) {
		m_node_cache.clear();
	}
	m_playback.start(clip);
	return true;
}

void SkeletalNode::stop() {
	m_playback.stop();
}

void SkeletalNode::init() {
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->registerDebugDraw(this, [](Node3D& node) {
			if (auto& skeletal = static_cast<SkeletalNode&>(node); skeletal.m_draw_bones) {
				skeletal.drawBones();
			}
		});
	}
}

void SkeletalNode::destroy() {
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->unregisterDebugDraw(this);
	}
}

void SkeletalNode::drawBones() {
	const glm::vec4 k_bone_color {1.0f, 0.6f, 0.1f, 1.0f};
	if (bindSkeleton() == nullptr) {
		return;
	}
	syncTransform();
	const glm::mat4& world = getWorldTransform();
	for (size_t bone = 0; bone < m_model.size(); ++bone) {
		const glm::vec3 position {world * m_model[bone][3]};
		if (m_parents[bone] >= 0) {
			debug::drawArrow(glm::vec3(world * m_model[m_parents[bone]][3]), position, k_bone_color);
		} else {
			debug::drawSphere(position, 0.03f, k_bone_color, 8);
		}
	}
}

void SkeletalNode::begin() {
	if (!m_autoplay.empty()) {
		play(m_autoplay);
	}
}

void SkeletalNode::updateInspectorMessages() {
	static const NodeMessage no_skeleton {
	  .severity = NodeMessage::warning,
	  .id = 1,
	  .text = "SkeletalNode needs a Skeleton asset",
	};

	if (skeleton() != nullptr) {
		removeInspectorMessage(no_skeleton);
	} else {
		addInspectorMessage(no_skeleton);
	}
}

auto SkeletalNode::inspectorOptions(std::string_view field_name) -> std::vector<std::string> {
	return field_name == "m_autoplay" ? clipNames(m_clips) : std::vector<std::string> {};
}

auto SkeletalNode::skeleton() -> const assets::Skeleton* {
	return bindSkeleton();
}

auto SkeletalNode::bindSkeleton() -> const assets::Skeleton* {
	const assets::Skeleton* found = m_skeleton.hasValue() ? &*m_skeleton : nullptr;
	if (found == m_bound) {
		return found;
	}

	m_bound = found;
	m_skin_bones.clear();
	m_track_clip = nullptr;
	m_track_to_bone.clear();

	const size_t count = found != nullptr ? found->bones().size() : 0;
	m_local.resize(count);
	m_model.assign(count, glm::mat4(1.0f));
	m_parents.resize(count);
	for (size_t i = 0; i < count; ++i) {
		m_local[i] = found->bones()[i].rest;
		m_parents[i] = found->bones()[i].parent;
	}
	if (found != nullptr) {
		computeModel(*found);
	}
	return found;
}

void SkeletalNode::bindTracks(const assets::Skeleton& skeleton) {
	if (m_track_clip == m_playback.clip) {
		return;
	}
	m_track_clip = m_playback.clip;
	m_track_to_bone.clear();
	if (m_track_clip == nullptr) {
		return;
	}

	m_track_to_bone.reserve(m_track_clip->tracks.size());
	for (const auto& track : m_track_clip->tracks) {
		m_track_to_bone.push_back(skeleton.find(track.target_node));
	}
}

void SkeletalNode::computeModel(const assets::Skeleton& skeleton) {
	for (size_t i = 0; i < m_local.size(); ++i) {
		const glm::mat4 local = poseMatrix(m_local[i]);
		const int32_t parent = skeleton.bones()[i].parent;
		m_model[i] = parent >= 0 ? m_model[parent] * local : local;
	}
}

void SkeletalNode::applyNodeTracks() {
	const auto* clip = m_playback.clip;
	for (size_t i = 0; i < clip->tracks.size() && i < m_track_to_bone.size(); ++i) {
		if (m_track_to_bone[i] >= 0) {
			continue;
		}
		const auto& track = clip->tracks[i];
		Node3D* target = resolveNode(track.target_node);
		if (target == nullptr) {
			continue;
		}
		switch (track.target) {
			case assets::TrackTarget::translation: target->position = track.sampleVec3(m_playback.time); break;
			case assets::TrackTarget::rotation: target->rotation = track.sampleQuat(m_playback.time); break;
			case assets::TrackTarget::scale: continue;
		}
		target->syncTransform();
	}
}

void SkeletalNode::applyAttachments(const assets::Skeleton& skeleton) {
	syncWorldTransform();
	const glm::mat4& skeletal_world = getWorldTransform();
	const glm::mat4 inverse_skeletal_world = glm::inverse(skeletal_world);

	for (const auto& child : children()) {
		auto* attachment = reflect_cast<BoneAttachment>(const_cast<Node*>(&*child));
		if (attachment == nullptr) {
			continue;
		}
		if (attachment->m_resolved_skeleton != &skeleton || attachment->m_resolved_name != attachment->m_bone) {
			attachment->m_resolved_skeleton = &skeleton;
			attachment->m_resolved_name = attachment->m_bone;
			attachment->m_bone_index = skeleton.find(attachment->m_bone);
		}
		const int32_t bone = attachment->m_bone_index;
		if (bone < 0 || std::cmp_greater_equal(bone, m_model.size())) {
			continue;
		}

		glm::vec3 skew {0.0f};
		glm::vec4 perspective {0.0f};
		glm::vec3 position {0.0f};
		glm::vec3 scale {1.0f};
		glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
		glm::mat4 local = m_model[bone];
		if (!attachment->m_inherit_scale) {
			if (!glm::decompose(skeletal_world * local, scale, rotation, position, skew, perspective)) {
				continue;
			}
			const glm::mat4 unscaled_world = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(glm::normalize(rotation));
			local = inverse_skeletal_world * unscaled_world;
		}
		if (!glm::decompose(local, scale, rotation, position, skew, perspective)) {
			continue;
		}
		attachment->position = position;
		attachment->rotation = rotation;
		attachment->scale = scale;
		attachment->syncTransform();
	}
}

auto SkeletalNode::resolveNode(const std::string& node_name) -> Node3D* {
	if (const auto it = m_node_cache.find(node_name); it != m_node_cache.end()) {
		return it->second.exists() ? &*it->second : nullptr;
	}

	// Descendants only since the importer may give this node the same name as an authored one
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

void SkeletalNode::tick() {
	ZoneScoped;
	const assets::Skeleton* skeleton = bindSkeleton();
	if (skeleton == nullptr) {
		return;
	}

	// Holds the last pose when paused or stopped so attachments still follow a moved skeleton asset
	if (m_playback.advance(static_cast<float>(Time::delta()), m_speed, m_loop)) {
		bindTracks(*skeleton);
		for (size_t i = 0; i < m_local.size(); ++i) {
			m_local[i] = skeleton->bones()[i].rest;
		}
		m_playback.clip->sampleInto(m_playback.time, m_track_to_bone, m_local);
		computeModel(*skeleton);
		applyNodeTracks();
	}

	applyAttachments(*skeleton);
}

auto SkeletalNode::boneIndex(std::string_view bone_name) -> int32_t {
	const assets::Skeleton* bound = bindSkeleton();
	return bound != nullptr ? bound->find(bone_name) : -1;
}

auto SkeletalNode::boneName(int32_t bone) const -> std::string_view {
	if (m_bound == nullptr || bone < 0 || std::cmp_greater_equal(bone, m_bound->bones().size())) {
		return {};
	}
	return m_bound->bones()[bone].name;
}

auto SkeletalNode::boneModelTransform(int32_t bone) const -> const glm::mat4& {
	if (bone < 0 || std::cmp_greater_equal(bone, m_model.size())) {
		return k_identity;
	}
	return m_model[bone];
}

auto SkeletalNode::boneWorldTransform(int32_t bone) -> glm::mat4 {
	syncTransform();
	return getWorldTransform() * boneModelTransform(bone);
}

auto SkeletalNode::skinBones(const assets::Skin& skin) -> std::span<const int32_t> {
	const assets::Skeleton* bound = bindSkeleton();
	if (bound == nullptr) {
		return {};
	}
	auto [it, inserted] = m_skin_bones.try_emplace(&skin);
	if (inserted) {
		it->second.reserve(skin.joints.size());
		for (const auto& joint : skin.joints) {
			it->second.push_back(bound->find(joint));
		}
	}
	return it->second;
}

}
