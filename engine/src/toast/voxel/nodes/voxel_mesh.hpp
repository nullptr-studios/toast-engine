/**
 * @file voxel_mesh.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief A voxel model inside a ProceduralVoxel
 */

#pragma once
#include "voxel_piece.hpp"

namespace assets {
class VoxelPalette;
}

namespace toast {

/** A voxel model inside a ProceduralVoxel, it has no physics of its own */
class [[ToastNode, Icon("MeshItem")]] TOAST_API VoxelMesh : public VoxelPiece {
public:
	[[nodiscard, Reflect]]
	auto getModel() const -> const assets::Handle<assets::VoxelModel>&;

	/** Swaps the model and redraws the piece */
	[[Reflect]]
	void setModel(assets::Handle<assets::VoxelModel> model);

	/** The palette that replaces the model colors */
	[[nodiscard, Reflect]]
	auto getPalette() const -> const assets::Handle<assets::VoxelPalette>&;

	[[Reflect]]
	void setPalette(assets::Handle<assets::VoxelPalette> palette);

	[[nodiscard]]
	auto sourcePaletteUid() const -> uint64_t override;

	/** The loaded model, null when there is none or the UID is not a voxel model */
	[[nodiscard]]
	auto resolvedModel() const -> const assets::VoxelModel*;

protected:
	auto prepareGrid() -> std::unique_ptr<voxel::Volume> override;

	[[Reflect, Name("Model")]]
	assets::Handle<assets::VoxelModel> m_model;

	[[Reflect, Name("Palette Override")]]
	assets::Handle<assets::VoxelPalette> m_palette;
};

}
