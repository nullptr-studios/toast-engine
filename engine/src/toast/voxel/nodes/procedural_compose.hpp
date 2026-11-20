/**
 * @file procedural_compose.hpp
 * @author Xein
 * @date 19 Nov 2026
 * @brief Builds only the modified parts of a ProceduralVoxel
 */

#pragma once
#include "voxel_node.hpp"
#include "voxel_piece.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/stamp.hpp>
#include <toast/voxel/voxel_edit.hpp>
#include <toast/world/workspace_events.hpp>
#include <utility>
#include <vector>

namespace toast::procedural {

inline constexpr uint64_t k_preview_key = std::numeric_limits<uint64_t>::max();
inline constexpr size_t k_max_part_keys = 255;

[[nodiscard]]
inline auto brickFloor(glm::ivec3 voxel) noexcept -> glm::ivec3 {
	return {voxel.x >> 3, voxel.y >> 3, voxel.z >> 3};
}

struct Stamp {
	uint64_t id = 0;
	std::shared_ptr<const voxel::Volume> grid;
	voxel::LatticePlacement placement;
	voxel::PaletteRemapTable remap {};
	PieceKind kind = PieceKind::fill;
	voxel::WriteMode mode = voxel::WriteMode::replace;
	uint8_t match_id = 0;
	uint64_t key = 0;

	/// Voxels the grid can write
	[[nodiscard]]
	TOAST_API auto bounds() const noexcept -> voxel::EditBounds;

	/// Writes exactly the same voxels as @p other
	[[nodiscard]]
	TOAST_API auto sameAs(const Stamp& other) const noexcept -> bool;
};

struct Request {
	std::vector<Stamp> stamps;
	std::optional<Stamp> preview;
	std::vector<std::pair<glm::ivec3, uint8_t>> buckets;
	std::shared_ptr<const voxel::Volume> base;
	const void* base_source = nullptr;
	bool projections = false;
	voxel::Palette palette;
	bool too_many_parts = false;
	std::optional<voxel::EditBounds> region;
};

struct Part {
	uint64_t key = 0;
	glm::ivec3 origin {0};
	std::unique_ptr<voxel::Volume> volume;
};

struct Result {
	std::vector<Part> parts;
	std::optional<voxel::EditBounds> region;
	std::vector<event::VoxelProjectionData> projections;
	bool has_projections = false;
	bool pool_exhausted = false;
	bool too_many_parts = false;
};

TOAST_API void assignPartKeys(Request& request);

[[nodiscard]]
TOAST_API auto compose(const Request& request) -> Result;

[[nodiscard]]
TOAST_API auto changedRegion(const Request& before, const Request& after) -> std::optional<voxel::EditBounds>;

struct ApplyStats {
	bool parts_changed = false;
	uint32_t changed = 0;
	uint32_t grown = 0;
	uint32_t left_out = 0;
	uint32_t clipped = 0;
};

/** Makes @p parts match a full result */
TOAST_API auto
    applyFull(std::vector<VoxelPart>& parts, Result& result, bool slack, std::vector<std::unique_ptr<voxel::Volume>>& retired)
        -> ApplyStats;

/** Makes the region of @p parts match and leaves the rest alone */
TOAST_API auto
    applyRegion(std::vector<VoxelPart>& parts, Result& result, bool slack, std::vector<std::unique_ptr<voxel::Volume>>& retired)
        -> ApplyStats;

}
