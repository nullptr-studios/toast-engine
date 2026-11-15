
#pragma once
#include "voxel_volume.hpp"

#include <atomic>

namespace assets {
class VoxelPalette;
}

namespace toast {

enum class LineAxis : uint8_t {
	x,
	y,
};

enum class LineSpace : uint8_t {
	local,
	world,
};

[[nodiscard]]
inline auto lineAxisFor(glm::ivec3 anchor, glm::ivec3 current) noexcept -> LineAxis {
	const glm::ivec3 moved = glm::abs(current - anchor);
	return moved.y > moved.x ? LineAxis::y : LineAxis::x;
}

class [[ToastNode, Icon("MeshItem")]] TOAST_API VoxelLine : public VoxelVolume {
public:
	[[nodiscard, Reflect]]
	auto getModel() const -> const assets::Handle<assets::VoxelModel>&;

	[[Reflect]]
	void setModel(assets::Handle<assets::VoxelModel> model);

	[[nodiscard, Reflect]]
	auto getPalette() const -> const assets::Handle<assets::VoxelPalette>&;

	[[Reflect]]
	void setPalette(assets::Handle<assets::VoxelPalette> palette);

	[[nodiscard]]
	auto getAxis() const noexcept -> LineAxis {
		return axis;
	}

	void setAxis(LineAxis value);

	[[nodiscard]]
	auto getTileSpace() const noexcept -> LineSpace {
		return tile_space;
	}

	void setTileSpace(LineSpace value);

	[[nodiscard]]
	auto getFlip() const noexcept -> bool {
		return flip;
	}

	void setFlip(bool value);

	[[nodiscard]]
	static auto tileSizeOf(const assets::Handle<assets::VoxelModel>& model, LineAxis along = LineAxis::x) -> glm::ivec3;

	[[nodiscard]]
	auto sourcePaletteUid() const -> uint64_t override;

	void landedAt(const voxel::LatticePlacement& placement) override;

protected:
	void init();

	void finishGrid(voxel::Volume& grid) override;
	void updateInspectorMessages() override;
	void onReflectedFieldChanged(std::string_view field_name) override;

	[[Reflect, Name("Model")]]
	assets::Handle<assets::VoxelModel> m_model;

	[[Reflect, Name("Palette Override")]]
	assets::Handle<assets::VoxelPalette> m_palette;

	[[Reflect, Name("Axis"), Enum("X", "Y")]]
	LineAxis axis = LineAxis::x;

	[[Reflect, Name("Tile Space"), Enum("Local", "World")]]
	LineSpace tile_space = LineSpace::local;

	[[Reflect, Name("Flip")]]
	bool flip = false;

	[[Reflect, Name("Tile Offset"), Unit("voxels")]]
	int tile_offset = 0;

private:
	void tileInto(voxel::Volume& grid);

	void refit(bool axis_changed);

	std::atomic<int32_t> m_anchor {0};
	std::atomic<bool> m_flipped {false};
};

}
