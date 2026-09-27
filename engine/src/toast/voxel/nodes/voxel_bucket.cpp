#include "voxel_bucket.hpp"

#include "procedural_voxel.hpp"
#include "voxel_node_utils.hpp"

namespace toast {

auto VoxelBucket::colorId() const noexcept -> uint8_t {
	return _detail::toId(id);
}

void VoxelBucket::updateInspectorMessages() {
	const NodeMessage outside {
	  .severity = NodeMessage::error, .id = _detail::k_message_outside, .text = "Only works inside a ProceduralVoxel"
	};
	if (insideProceduralVoxel(*this)) {
		removeInspectorMessage(outside);
	} else {
		addInspectorMessage(outside);
	}
}

auto VoxelBucket::getShape() -> Box<Node> {
	ProceduralVoxel* shape = owningProceduralVoxel(*this);
	return shape != nullptr ? shape->box() : Box<Node> {};
}

}
