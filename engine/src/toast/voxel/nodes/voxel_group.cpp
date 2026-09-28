#include "voxel_group.hpp"

#include "procedural_voxel.hpp"
#include "voxel_node_utils.hpp"

namespace toast {

void VoxelGroup::updateInspectorMessages() {
	const NodeMessage outside {
	  .severity = NodeMessage::error, .id = _detail::k_message_outside, .text = "Only works inside a ProceduralVoxel"
	};
	if (insideProceduralVoxel(*this)) {
		removeInspectorMessage(outside);
	} else {
		addInspectorMessage(outside);
	}
}

auto VoxelGroup::getShape() -> Box<Node> {
	ProceduralVoxel* shape = owningProceduralVoxel(*this);
	return shape != nullptr ? shape->box() : Box<Node> {};
}

}
