/**
 * @file voxel_volume.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief A box of voxels inside a ProceduralVoxel
 */

#pragma once
#include "voxel_piece.hpp"

namespace toast {

/**
 * A box of voxels drawn by its first script
 * Scripts get the size and cannot change it
 */
class [[ToastNode, Hidden, Interface]] TOAST_API VoxelVolume : public VoxelPiece {
public:
	/** @returns the palette ID the volume draws with */
	[[nodiscard, Reflect]]
	auto getId() const -> int;

	/** Changes the palette ID the volume draws with and redraws it */
	[[Reflect]]
	void setId(int value);

	/** Resizes the volume in voxels, every axis is at least 1 */
	[[Reflect]]
	void setSize(glm::vec3 value);

	/** @returns what the volume is allowed to overwrite */
	[[nodiscard, Reflect]]
	auto getMode() const -> voxel::WriteMode;

	[[Reflect]]
	void setMode(voxel::WriteMode value);

	/** @returns the only palette ID that Match overwrites */
	[[nodiscard, Reflect]]
	auto getMatchId() const -> int;

	[[Reflect]]
	void setMatchId(int value);

	// Backend

	[[nodiscard]]
	auto sizeVoxels() const noexcept -> glm::ivec3;

	void setSizeVoxels(glm::ivec3 size);

	[[nodiscard]]
	auto resizable() const noexcept -> bool override {
		return true;
	}

	[[nodiscard]]
	auto writeMode() const noexcept -> voxel::WriteMode override {
		return mode;
	}

	[[nodiscard]]
	auto matchId() const noexcept -> uint8_t override;

protected:
	void init();

	auto prepareGrid() -> std::unique_ptr<voxel::Volume> override;
	void finishGrid(voxel::Volume& grid) override;
	auto pieceSize() -> glm::ivec3 override;
	void onReflectedFieldChanged(std::string_view field_name) override;

	[[Reflect, Name("Size"), Unit("voxels")]]
	glm::vec3 size = glm::vec3(10.0f);

	/** Palette ID that scripts draw with */
	[[Reflect, Name("Color")]]
	int id = 1;

	[[Reflect, Name("Fill Mode"), Enum("Replace", "Empty Only", "Solid Only", "Match")]]
	voxel::WriteMode mode = voxel::WriteMode::replace;

	/** The only palette ID that Match overwrites */
	[[Reflect, Name("Match Color")]]
	int match_id = 0;
};

}
