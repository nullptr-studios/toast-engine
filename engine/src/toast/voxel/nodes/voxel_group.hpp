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

	/** @returns true when everything inside the group becomes one voxel volume */
	[[nodiscard, Reflect]]
	auto mergesVolumes() const noexcept -> bool {
		return m_merge_volumes;
	}

protected:
	void updateInspectorMessages() override;

	/**
	 * Makes everything inside the group one voxel volume
	 * @note It renders, collides, breaks and bakes as a single volume, the pieces still apply in order and keep their fill modes
	 */
	[[Reflect, Name("Merge Volumes")]]
	bool m_merge_volumes = false;
};

}
