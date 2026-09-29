/**
 * @file voxel_group.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief Keeps pieces of a ProceduralVoxel together
 */

#pragma once
#include "voxel.hpp"

namespace toast {

/** Keeps pieces together, its children are applied in order where it sits */
class [[ToastNode, Icon("Container"), Color("Magenta")]] TOAST_API VoxelGroup : public Voxel {
public:
	/** @returns the ProceduralVoxel the group is built into, through any groups */
	[[nodiscard, Reflect]]
	auto getShape() -> Box<Node>;

protected:
	void updateInspectorMessages() override;
};

}
