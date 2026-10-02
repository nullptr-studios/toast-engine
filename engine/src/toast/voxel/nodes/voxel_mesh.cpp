#include "voxel_mesh.hpp"

#include "voxel_node_utils.hpp"

#include <toast/voxel/assets/voxel_palette.hpp>

namespace toast {

using _detail::assetOfType;
using _detail::instantiateModel;

auto VoxelMesh::getModel() const -> const assets::Handle<assets::VoxelModel>& {
	return m_model;
}

void VoxelMesh::setModel(assets::Handle<assets::VoxelModel> model) {
	m_model = std::move(model);
	markGridDirty();
}

auto VoxelMesh::getPalette() const -> const assets::Handle<assets::VoxelPalette>& {
	return m_palette;
}

void VoxelMesh::setPalette(assets::Handle<assets::VoxelPalette> palette) {
	m_palette = std::move(palette);
	markLandingDirty();
}

auto VoxelMesh::sourcePaletteUid() const -> uint64_t {
	if (m_palette.uid().data() != 0) {
		return m_palette.uid().data();
	}
	const auto* model = assetOfType(m_model, "voxel_model");
	return model != nullptr ? model->paletteUid() : 0;
}

auto VoxelMesh::resolvedModel() const -> const assets::VoxelModel* {
	return assetOfType(m_model, "voxel_model");
}

auto VoxelMesh::pieceSize() -> glm::ivec3 {
	const auto* model = assetOfType(m_model, "voxel_model");
	return model != nullptr ? glm::ivec3(model->brickDims()) * static_cast<int32_t>(voxel::k_brick_dim) : glm::ivec3(0);
}

auto VoxelMesh::prepareGrid() -> std::unique_ptr<voxel::Volume> {
	const auto* model = assetOfType(m_model, "voxel_model");
	return model != nullptr ? instantiateModel(*model) : nullptr;
}

}
