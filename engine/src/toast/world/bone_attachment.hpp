/**
 * @file bone_attachment.hpp
 * @author dario
 * @date 9 Nov 2026
 */

#pragma once
#include "node_3d.hpp"

#include <string>

namespace assets {
struct Skeleton;
}

namespace toast {

/// Follows one bone of its parent SkeletalNode, offsets go on its children
class [[ToastNode, Icon("BoneAttachment")]] TOAST_API BoneAttachment : public Node3D {
	friend class SkeletalNode;

public:
	BoneAttachment() = default;

	[[nodiscard]]
	auto bone() const noexcept -> const std::string& {
		return m_bone;
	}

	void bone(std::string name) {
		m_bone = std::move(name);
		m_resolved_skeleton = nullptr;
	}

private:
	void updateInspectorMessages() override;
	auto inspectorOptions(std::string_view field_name) -> std::vector<std::string> override;

	[[Reflect, Name("Bone"), Options]]
	std::string m_bone;

	/// Off keeps world scale 1 since physics rejects voxel nodes with non unit world scale
	[[Reflect, Name("Inherit Scale")]]
	bool m_inherit_scale = false;

	const assets::Skeleton* m_resolved_skeleton = nullptr;
	std::string m_resolved_name;
	int32_t m_bone_index = -1;
};

}
