/**
 * @file voxel_node.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "node_3d.hpp"

#include <cstdint>
#include <optional>
#include <toast/assets/types.hpp>
#include <toast/voxel/stamp.hpp>

namespace assets {
class VoxelModel;
class VoxelPalette;
class VoxelMaterialLibrary;
}

namespace toast {

enum class VoxelMobility : uint8_t {
	/// Must sit on the lattice with a whole voxel offset and one of the 48 orientations
	static_geometry = 0,

	dynamic = 1,
};

class [[ToastNode, Icon("BoxMesh")]] TOAST_API VoxelNode : public Node3D {
public:
	VoxelNode() = default;

	VoxelNode(assets::Handle<assets::VoxelModel> model) : m_model(std::move(model)) { }

	[[nodiscard]]
	auto getModel() const -> const assets::Handle<assets::VoxelModel>& {
		return m_model;
	}

	[[nodiscard]]
	auto getModel() -> assets::Handle<assets::VoxelModel>& {
		return m_model;
	}

	void setModel(assets::Handle<assets::VoxelModel> model);

	[[nodiscard]]
	auto getPalette() const -> const assets::Handle<assets::VoxelPalette>& {
		return m_palette;
	}

	void setPalette(assets::Handle<assets::VoxelPalette> palette);

	[[nodiscard]]
	auto mobility() const noexcept -> VoxelMobility {
		return m_mobility;
	}

	void setMobility(VoxelMobility mobility) noexcept { m_mobility = mobility; }

	/// @returns the override else the model palette else 0
	[[nodiscard]]
	auto paletteUid() const -> uint64_t;

	/// @returns centre in xyz radius in w or zero without a model
	[[nodiscard]]
	auto localBoundingSphere() const -> glm::vec4;

	/// @note Call syncTransform() first
	[[nodiscard]]
	auto latticePlacement() const -> std::optional<voxel::LatticePlacement>;

	/// @note Lazy. Main thread only
	[[nodiscard]]
	auto volume() -> voxel::Volume*;

	[[nodiscard]]
	auto resolvedPalette() -> const voxel::Palette*;
	[[nodiscard]]
	auto resolvedModel() const -> const assets::VoxelModel*;
	[[nodiscard]]
	auto resolvedMaterialLibrary() -> const voxel::MaterialLibrary*;

	[[nodiscard]]
	auto revision() const noexcept -> uint32_t {
		return m_revision;
	}

private:
	void init();
	void begin();
	void end();
	void destroy();

	void releaseVolume();

	[[Reflect, Name("Model")]]
	assets::Handle<assets::VoxelModel> m_model;

	[[Reflect, Name("Palette Override")]]
	assets::Handle<assets::VoxelPalette> m_palette;

	[[Reflect, Name("Mobility"), Enum("Static", "Dynamic")]]
	VoxelMobility m_mobility = VoxelMobility::static_geometry;

	bool m_registered_proxy = false;

	std::optional<voxel::Volume> m_volume;

	const assets::VoxelModel* m_instanced_from = nullptr;

	assets::Handle<assets::VoxelPalette> m_model_palette;
	assets::Handle<assets::VoxelMaterialLibrary> m_material_library;

	uint32_t m_revision = 0;

	uint64_t m_reported_wrong_model = 0;
	uint64_t m_reported_wrong_palette = 0;
};

}
