#include "voxel_line.hpp"

#include "voxel_node_utils.hpp"

#include <toast/voxel/assets/voxel_palette.hpp>
#include <toast/voxel/voxel_edit.hpp>

namespace toast {

namespace {

using _detail::instantiateModel;
using _detail::voxelModelOf;

[[nodiscard]]
auto floorMod(int32_t value, int32_t divisor) noexcept -> int32_t {
	const int32_t out = value % divisor;
	return out < 0 ? out + divisor : out;
}

[[nodiscard]]
auto bricksFor(int32_t voxels) noexcept -> uint32_t {
	const auto dim = static_cast<int32_t>(voxel::k_brick_dim);
	return static_cast<uint32_t>((voxels + dim - 1) / dim);
}

[[nodiscard]]
auto mirroredAcross(const voxel::Volume& pattern, const voxel::EditBounds& solid, int axis) -> std::unique_ptr<voxel::Volume> {
	const glm::ivec3 size = solid.max - solid.min + 1;
	auto mirrored = std::make_unique<voxel::Volume>(
	    voxel::proceduralBrickPool(), glm::uvec3(bricksFor(size.x), bricksFor(size.y), bricksFor(size.z))
	);
	for (int32_t z = 0; z < size.z; ++z) {
		for (int32_t y = 0; y < size.y; ++y) {
			for (int32_t x = 0; x < size.x; ++x) {
				const glm::ivec3 cell {x, y, z};
				const uint8_t id = pattern.materialAt(solid.min + cell);
				if (id != voxel::k_empty_palette_index) {
					glm::ivec3 to = cell;
					to[axis] = size[axis] - 1 - cell[axis];
					mirrored->setVoxel(to, id);
				}
			}
		}
	}
	return mirrored;
}

[[nodiscard]]
auto turnedAboutZ(const voxel::Volume& pattern, const voxel::EditBounds& solid) -> std::unique_ptr<voxel::Volume> {
	const glm::ivec3 size = solid.max - solid.min + 1;
	auto turned = std::make_unique<voxel::Volume>(
	    voxel::proceduralBrickPool(), glm::uvec3(bricksFor(size.y), bricksFor(size.x), bricksFor(size.z))
	);
	for (int32_t z = 0; z < size.z; ++z) {
		for (int32_t y = 0; y < size.y; ++y) {
			for (int32_t x = 0; x < size.x; ++x) {
				const uint8_t id = pattern.materialAt(solid.min + glm::ivec3(x, y, z));
				if (id != voxel::k_empty_palette_index) {
					turned->setVoxel({size.y - 1 - y, x, z}, id);
				}
			}
		}
	}
	return turned;
}

}

auto VoxelLine::getModel() const -> const assets::Handle<assets::VoxelModel>& {
	return m_model;
}

void VoxelLine::setModel(assets::Handle<assets::VoxelModel> model) {
	m_model = std::move(model);
	markGridDirty();
	refit(false);
}

void VoxelLine::refit(bool axis_changed) {
	const glm::ivec3 tile = tileSizeOf(m_model, axis);
	if (glm::any(glm::lessThan(tile, glm::ivec3(1)))) {
		return;
	}
	glm::ivec3 size = sizeVoxels();
	const int along = axis == LineAxis::y ? 1 : 0;
	if (axis_changed) {
		size[along] = std::max(size.x, size.y);
	}
	for (int a = 0; a < 3; ++a) {
		if (a != along) {
			size[a] = tile[a];
		}
	}
	setSizeVoxels(size);
}

void VoxelLine::onReflectedFieldChanged(std::string_view field_name) {
	VoxelVolume::onReflectedFieldChanged(field_name);
	if (field_name == "axis") {
		refit(true);
	} else if (field_name == "m_model") {
		refit(false);
	}
}

auto VoxelLine::getPalette() const -> const assets::Handle<assets::VoxelPalette>& {
	return m_palette;
}

void VoxelLine::setPalette(assets::Handle<assets::VoxelPalette> palette) {
	m_palette = std::move(palette);
	markLandingDirty();
}

void VoxelLine::setAxis(LineAxis value) {
	if (axis != value) {
		axis = value;
		markGridDirty();
		refit(true);
	}
}

void VoxelLine::setFlip(bool value) {
	if (flip != value) {
		flip = value;
		markGridDirty();
	}
}

void VoxelLine::setTileSpace(LineSpace value) {
	if (tile_space != value) {
		tile_space = value;
		markGridDirty();
	}
}

auto VoxelLine::tileSizeOf(const assets::Handle<assets::VoxelModel>& model, LineAxis along) -> glm::ivec3 {
	const auto* source = voxelModelOf(model);
	const std::unique_ptr<voxel::Volume> pattern = source != nullptr ? instantiateModel(*source) : nullptr;
	const std::optional<voxel::EditBounds> solid = pattern != nullptr ? voxel::occupiedBounds(*pattern) : std::nullopt;
	if (!solid.has_value()) {
		return glm::ivec3(0);
	}
	const glm::ivec3 size = solid->max - solid->min + 1;
	return along == LineAxis::y ? glm::ivec3(size.y, size.x, size.z) : size;
}

auto VoxelLine::sourcePaletteUid() const -> uint64_t {
	return m_palette.uid().data();
}

void VoxelLine::init() {
	if (!scripts().empty()) {
		setShapeScript({});
	}
}

void VoxelLine::landedAt(const voxel::LatticePlacement& placement) {
	if (tile_space != LineSpace::world) {
		return;
	}
	const auto local_axis = static_cast<uint8_t>(axis == LineAxis::y ? 1 : 0);
	size_t shape_axis = 0;
	for (size_t i = 0; i < 3; ++i) {
		if (placement.orientation.source[i] == local_axis) {
			shape_axis = i;
		}
	}
	const int32_t anchor = placement.offset[static_cast<int>(shape_axis)];
	const bool flipped = placement.orientation.flip[shape_axis];
	if (anchor != m_anchor.load() || flipped != m_flipped.load()) {
		m_anchor.store(anchor);
		m_flipped.store(flipped);
		markGridDirty();
	}
}

void VoxelLine::finishGrid(voxel::Volume& grid) {
	const voxel::WriteBrush empty {.id = voxel::k_empty_palette_index};
	voxel::fillBox(grid, glm::ivec3(0), glm::ivec3(grid.voxelDims()) - 1, empty);

	tileInto(grid);

	VoxelVolume::finishGrid(grid);
}

void VoxelLine::tileInto(voxel::Volume& grid) {
	const auto* model = voxelModelOf(m_model);
	if (model == nullptr) {
		return;
	}
	const std::unique_ptr<voxel::Volume> pattern = instantiateModel(*model);
	std::optional<voxel::EditBounds> solid = pattern != nullptr ? voxel::occupiedBounds(*pattern) : std::nullopt;
	if (!solid.has_value()) {
		return;
	}

	const voxel::Volume* repeated = pattern.get();
	std::unique_ptr<voxel::Volume> turned;
	if (axis == LineAxis::y) {
		turned = turnedAboutZ(*pattern, *solid);
		solid = voxel::occupiedBounds(*turned);
		repeated = turned.get();
		if (!solid.has_value()) {
			return;
		}
	}

	std::unique_ptr<voxel::Volume> mirrored;
	if (flip) {
		mirrored = mirroredAcross(*repeated, *solid, axis == LineAxis::y ? 0 : 1);
		solid = voxel::occupiedBounds(*mirrored);
		repeated = mirrored.get();
		if (!solid.has_value()) {
			return;
		}
	}

	const int line_axis = axis == LineAxis::y ? 1 : 0;
	int32_t phase = 0;
	if (tile_space == LineSpace::world) {
		const int32_t tile = solid->max[line_axis] - solid->min[line_axis] + 1;
		phase = m_flipped.load() ? floorMod(-m_anchor.load(), tile) : m_anchor.load();
	}
	voxel::tileLine(
	    grid, *repeated, line_axis, glm::ivec3(0), sizeVoxels()[line_axis], phase + tile_offset, voxel::identityRemap()
	);
}

void VoxelLine::updateInspectorMessages() {
	VoxelPiece::updateInspectorMessages();
	const NodeMessage no_model {
	  .severity = NodeMessage::warning, .id = _detail::k_message_no_model, .text = "Pick a voxel model to repeat"
	};
	if (voxelModelOf(m_model) == nullptr) {
		addInspectorMessage(no_model);
	} else {
		removeInspectorMessage(no_model);
	}
}

}
