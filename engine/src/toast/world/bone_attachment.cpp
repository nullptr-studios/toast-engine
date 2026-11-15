#include "bone_attachment.hpp"

#include "skeletal_node.hpp"

namespace toast {

auto BoneAttachment::inspectorOptions(std::string_view field_name) -> std::vector<std::string> {
	std::vector<std::string> options;
	auto skeletal = parent().as<SkeletalNode>();
	if (field_name != "m_bone" || !skeletal.exists()) {
		return options;
	}
	if (const assets::Skeleton* skeleton = skeletal->skeleton(); skeleton != nullptr) {
		options.reserve(skeleton->bones().size());
		for (const auto& bone : skeleton->bones()) {
			options.push_back(bone.name);
		}
	}
	return options;
}

void BoneAttachment::updateInspectorMessages() {
	static const NodeMessage no_skeleton {
	  .severity = NodeMessage::warning,
	  .id = 1,
	  .text = "BoneAttachment must be a direct child of a SkeletalNode",
	};
	static const NodeMessage no_bone {
	  .severity = NodeMessage::warning,
	  .id = 2,
	  .text = "Bone not found in the parent skeleton",
	};

	auto skeletal = parent().as<SkeletalNode>();
	if (!skeletal.exists()) {
		addInspectorMessage(no_skeleton);
		removeInspectorMessage(no_bone);
		return;
	}
	removeInspectorMessage(no_skeleton);

	if (skeletal->boneIndex(m_bone) >= 0) {
		removeInspectorMessage(no_bone);
	} else {
		addInspectorMessage(no_bone);
	}
}

}
