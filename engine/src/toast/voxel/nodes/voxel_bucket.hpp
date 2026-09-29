/**
 * @file voxel_bucket.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief A flood fill inside a ProceduralVoxel
 */

#pragma once
#include "voxel.hpp"

#include <cstdint>

namespace toast {

/**
 * Repaints the solid voxels connected to where it sits that share that voxel colour
 * @note Every bucket runs after all the pieces, in hierarchy order, so moving pieces never breaks it
 */
class [[ToastNode, Icon("Bucket"), Color("Magenta")]] TOAST_API VoxelBucket : public Voxel {
public:
	/** @returns the palette ID it paints with */
	[[nodiscard, Reflect]]
	auto getId() const noexcept -> int {
		return id;
	}

	/** Changes the palette ID it paints with */
	[[Reflect]]
	void setId(int value) noexcept {
		id = value;
	}

	/** @returns the ProceduralVoxel the bucket paints, through any groups */
	[[nodiscard, Reflect]]
	auto getShape() -> Box<Node>;

	// Backend

	[[nodiscard]]
	auto colorId() const noexcept -> uint8_t;

protected:
	void updateInspectorMessages() override;

	/** Palette ID it paints with */
	[[Reflect, Name("Color")]]
	int id = 1;
};

}
