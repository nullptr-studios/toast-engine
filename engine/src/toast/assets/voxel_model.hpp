/**
 * @file voxel_model.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "core_types.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string_view>
#include <toast/voxel/brick_pool.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <toast/voxel/voxel_volume.hpp>
#include <vector>

namespace assets {

namespace _detail {

/// The header defaults from this so the writer and reader cannot disagree
inline constexpr uint16_t voxel_format_version = 1;

/// @brief Read before the version check so never add fields
/// Body uint64 palette UID then uint32 stored brick count then uint32 per brick slot x fastest then 512 bytes per stored brick
struct VoxelFileHeader {
	std::array<uint8_t, 6> magic = {'T', 'V', 'O', 'X', '\0', '\0'};
	uint16_t version = voxel_format_version;
	uint16_t dim_x = 0;
	uint16_t dim_y = 0;
	uint16_t dim_z = 0;
	uint16_t reserved = 0;
};

static_assert(sizeof(VoxelFileHeader) == 16, "the .tvox header must stay sixteen bytes");
static_assert(VoxelFileHeader {}.version == voxel_format_version, "the header must default from the version constant");

}

class TOAST_API VoxelModel : public Asset, public ISaveable {
public:
	/// @brief Throws on malformed or non canonical data
	explicit VoxelModel(const std::vector<uint8_t>& data);

	[[nodiscard]]
	static auto capture(const voxel::Volume& volume, uint64_t palette_uid) -> std::unique_ptr<VoxelModel>;

	[[nodiscard]]
	auto instantiate(voxel::BrickPool& pool) const -> std::optional<voxel::Volume>;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "voxel_model";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto brickDims() const noexcept -> glm::uvec3 {
		return m_brick_dims;
	}

	[[nodiscard]]
	auto paletteUid() const noexcept -> uint64_t {
		return m_palette_uid;
	}

	[[nodiscard]]
	auto storedBrickCount() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_bricks.size() / voxel::k_brick_material_bytes);
	}

	[[nodiscard]]
	auto solidVoxelCount() const -> uint32_t;

private:
	VoxelModel() = default;

	glm::uvec3 m_brick_dims {0};
	uint64_t m_palette_uid = 0;

	/// Stored bricks are tagged owned with the stored brick index as payload
	std::vector<voxel::BrickEntry> m_grid;

	std::vector<uint8_t> m_bricks;
};

}
