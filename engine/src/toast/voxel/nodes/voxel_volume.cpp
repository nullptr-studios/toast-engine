#include "voxel_volume.hpp"

#include "voxel_node.hpp"
#include "voxel_node_utils.hpp"

#include <toast/assets/script.hpp>
#include <toast/log.hpp>

namespace toast {

namespace {

constexpr std::string_view k_default_volume_script = "core://VoxelScripts/DrawBox.lua";

using _detail::toId;

}

auto VoxelVolume::getId() const -> int {
	return id;
}

auto VoxelVolume::sizeVoxels() const noexcept -> glm::ivec3 {
	return glm::max(glm::ivec3(glm::round(size)), glm::ivec3(1));
}

auto VoxelVolume::pieceSize() -> glm::ivec3 {
	return sizeVoxels();
}

void VoxelVolume::setSizeVoxels(glm::ivec3 new_size) {
	size = glm::vec3(glm::max(new_size, glm::ivec3(1)));
	markGridDirty();
}

auto VoxelVolume::matchId() const noexcept -> uint8_t {
	return toId(match_id);
}

void VoxelVolume::init() {
	if (scripts().empty()) {
		const assets::Handle<assets::Script> script = assets::load<assets::Script>(k_default_volume_script);
		if (script.hasValue()) {
			setShapeScript(script);
		} else {
			TOAST_WARN("Voxel", "'{}' could not load its default script {}", name(), k_default_volume_script);
		}
	}
}

auto VoxelVolume::prepareGrid() -> std::unique_ptr<voxel::Volume> {
	const glm::ivec3 voxels = sizeVoxels();
	const glm::uvec3 bricks =
	    glm::uvec3((voxels + static_cast<int32_t>(voxel::k_brick_dim) - 1) / static_cast<int32_t>(voxel::k_brick_dim));
	return std::make_unique<voxel::Volume>(voxel::proceduralBrickPool(), bricks);
}

void VoxelVolume::finishGrid(voxel::Volume& grid) {
	// Bricks round the grid up so empty everything past the size scripts were given
	const glm::ivec3 voxels = sizeVoxels();
	const glm::ivec3 dims = glm::ivec3(grid.voxelDims());
	const voxel::WriteBrush empty {.id = voxel::k_empty_palette_index};
	if (voxels.x < dims.x) {
		voxel::fillBox(grid, {voxels.x, 0, 0}, dims - 1, empty);
	}
	if (voxels.y < dims.y) {
		voxel::fillBox(grid, {0, voxels.y, 0}, dims - 1, empty);
	}
	if (voxels.z < dims.z) {
		voxel::fillBox(grid, {0, 0, voxels.z}, dims - 1, empty);
	}
}

void VoxelVolume::onReflectedFieldChanged(std::string_view field_name) {
	if (field_name == "mode" || field_name == "match_id") {
		// Skips VoxelPiece on purpose, these only change how the grid lands so it must not be redrawn
		Node3D::onReflectedFieldChanged(field_name);    // NOLINT(bugprone-parent-virtual-call)
		markLandingDirty();
		return;
	}
	VoxelPiece::onReflectedFieldChanged(field_name);
}

void VoxelVolume::setId(int value) {
	if (id != value) {
		id = value;
		markGridDirty();
	}
}

void VoxelVolume::setSize(glm::vec3 value) {
	setSizeVoxels(glm::ivec3(glm::round(value)));
}

auto VoxelVolume::getMode() const -> voxel::WriteMode {
	return mode;
}

void VoxelVolume::setMode(voxel::WriteMode value) {
	if (mode != value) {
		mode = value;
		markLandingDirty();
	}
}

auto VoxelVolume::getMatchId() const -> int {
	return match_id;
}

void VoxelVolume::setMatchId(int value) {
	if (match_id != value) {
		match_id = value;
		markLandingDirty();
	}
}

}
