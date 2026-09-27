/**
 * @file voxel_edit.hpp
 * @author Xein
 * @date 25 Sep 2026
 * @brief Functions that allow the editing of Voxel Volumes
 */

#pragma once
#include "stamp.hpp"
#include "voxel_volume.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <toast/export.hpp>
#include <vector>

namespace voxel {

/**
 * @brief Which voxels we should override
 * @note On lua this is called VoxelWrite
 */
enum class WriteMode : uint8_t {
	replace,       ///< Everything
	empty_only,    ///< Only empty space
	solid_only,    ///< Only non-empty space
	match,         ///< Only voxels with the match id
};

[[nodiscard]]
constexpr auto canWrite(uint8_t current, WriteMode mode, uint8_t match_id) noexcept -> bool {
	switch (mode) {
		case WriteMode::replace: return true;
		case WriteMode::empty_only: return current == k_empty_palette_index;
		case WriteMode::solid_only: return current != k_empty_palette_index;
		case WriteMode::match: return current == match_id;
	}
	return false;
}

[[nodiscard]]
constexpr auto canAdd(uint8_t id, WriteMode mode, uint8_t match_id) noexcept -> bool {
	return id != k_empty_palette_index && canWrite(k_empty_palette_index, mode, match_id);
}

struct EditBounds {
	glm::ivec3 min {0};
	glm::ivec3 max {-1};

	[[nodiscard]]
	auto empty() const noexcept -> bool {
		return glm::any(glm::lessThan(max, min));
	}
};

struct EditResult {
	uint32_t changed = 0;

	bool pool_exhausted = false;

	auto operator+=(const EditResult& other) noexcept -> EditResult& {
		changed += other.changed;
		pool_exhausted = pool_exhausted || other.pool_exhausted;
		return *this;
	}
};

struct WriteBrush {
	uint8_t id = k_empty_palette_index;
	WriteMode mode = WriteMode::replace;
	uint8_t match_id = k_empty_palette_index;
};

struct Region {
	glm::ivec3 size {0};
	std::vector<uint8_t> ids;
};

// Bounds calculation
// I'm using this to know when to grow the volume

[[nodiscard]]
TOAST_API auto boxBounds(glm::ivec3 a, glm::ivec3 b) noexcept -> EditBounds;

[[nodiscard]]
TOAST_API auto sphereBounds(glm::vec3 center, float radius) noexcept -> EditBounds;

[[nodiscard]]
TOAST_API auto segmentBounds(glm::vec3 a, glm::vec3 b, float radius) noexcept -> EditBounds;

[[nodiscard]]
TOAST_API auto placedBounds(glm::uvec3 piece_brick_dims, const LatticePlacement& placement) noexcept -> EditBounds;

[[nodiscard]]
TOAST_API auto boundsUnion(const EditBounds& a, const EditBounds& b) noexcept -> EditBounds;

[[nodiscard]]
TOAST_API auto occupiedBounds(const Volume& volume) -> std::optional<EditBounds>;

// Primitives

TOAST_API auto fillBox(Volume& volume, glm::ivec3 a, glm::ivec3 b, const WriteBrush& brush) -> EditResult;
TOAST_API auto fillSphere(Volume& volume, glm::vec3 center, float radius, const WriteBrush& brush) -> EditResult;
TOAST_API auto fillCylinder(Volume& volume, glm::vec3 a, glm::vec3 b, float radius, const WriteBrush& brush) -> EditResult;
TOAST_API auto fillCapsule(Volume& volume, glm::vec3 a, glm::vec3 b, float radius, const WriteBrush& brush) -> EditResult;

// Basic operations

/**
 * Replaces all of the nodes of one ID to another
 * @param from Target palette ID
 * @param to New palette ID
 */
TOAST_API auto replaceId(Volume& volume, uint8_t from, uint8_t to) -> EditResult;

/**
 * Cuts a volume by a plane
 * @param point Point of the plane
 * @param normal Normal of the plane
 * @param side +1 (normal direction) or -1 (antinormal direction)
 */
TOAST_API auto slice(Volume& volume, glm::vec3 point, glm::vec3 normal, int side) -> EditResult;

/**
 * Returns the voxels with the same normal
 */
[[nodiscard]]
TOAST_API auto collectFace(const Volume& volume, glm::ivec3 start, glm::ivec3 normal, size_t limit = size_t {1} << 20)
    -> std::vector<glm::ivec3>;

/**
 * Pushes a face out along its normal, carves when negative distance
 */
TOAST_API auto extrudeFace(Volume& volume, std::span<const glm::ivec3> face, glm::ivec3 normal, int distance) -> EditResult;

[[nodiscard]]
TOAST_API auto copyRegion(const Volume& volume, glm::ivec3 a, glm::ivec3 b) -> Region;

TOAST_API auto pasteRegion(Volume& volume, const Region& region, glm::ivec3 at, WriteMode mode, uint8_t match_id) -> EditResult;

TOAST_API auto stampVolume(
    Volume& target, const Volume& piece, const LatticePlacement& placement, const PaletteRemapTable& remap, WriteMode mode,
    uint8_t match_id
) -> EditResult;

/**
 * Empties every target voxel the piece has a solid voxel on
 * @note The carve counterpart of stampVolume
 */
TOAST_API auto carveVolume(Volume& target, const Volume& piece, const LatticePlacement& placement) -> EditResult;

// Shapes that fit a box instead of a radius

/** Fills the box from a to b with its edges rounded by radius */
TOAST_API auto fillRoundBox(Volume& volume, glm::ivec3 a, glm::ivec3 b, float radius, const WriteBrush& brush) -> EditResult;

/** Fills the ellipsoid that touches every face of the box from a to b */
TOAST_API auto fillEllipsoid(Volume& volume, glm::ivec3 a, glm::ivec3 b, const WriteBrush& brush) -> EditResult;

// Clip planes
//
// A plane is a vec4 with the normal in xyz and the offset in w, in voxels
// A voxel is kept when dot(normal, centre) + w >= 0

/** Empties every voxel that is on the wrong side of any plane */
TOAST_API auto clipByPlanes(Volume& volume, std::span<const glm::vec4> planes) -> EditResult;

/** The plane that keeps what plane drops */
[[nodiscard]]
TOAST_API auto complementPlane(glm::vec4 plane) noexcept -> glm::vec4;

// Tiling

enum class TileFit : uint8_t {
	crop,
	stretch,
	center,
};

enum class TileAnchor : uint8_t {
	min,
	center,
	max,
};

struct TileOptions {
	TileFit fit = TileFit::crop;
	std::array<TileAnchor, 3> anchor {TileAnchor::min, TileAnchor::min, TileAnchor::min};
	glm::ivec3 offset {0};        ///< Shifts the tiles by this many voxels
	glm::bvec3 mirror {false};    ///< Flips every other tile on that axis
};

/**
 * Repeats the solid part of pattern across the box from a to b
 * @param remap Maps pattern ids to target ids and 0 skips the voxel
 */
TOAST_API auto tileVolume(
    Volume& target, const Volume& pattern, glm::ivec3 a, glm::ivec3 b, const TileOptions& options, const PaletteRemapTable& remap,
    WriteMode mode = WriteMode::replace, uint8_t match_id = k_empty_palette_index
) -> EditResult;

/** Repaints the solid voxels connected to seed that share its id */
TOAST_API auto floodFill(Volume& volume, glm::ivec3 seed, uint8_t id) -> EditResult;

struct VolumeHit {
	glm::ivec3 voxel {0};     ///< The solid voxel it stopped in
	glm::ivec3 normal {0};    ///< The face it came through, zero when it started inside
	float t = 0.0f;
};

/**
 * Walks a ray through the volume one voxel at a time
 * @param origin In voxels, a voxel spans [v, v + 1]
 * @returns the first solid voxel within max_t
 */
[[nodiscard]]
TOAST_API auto raycast(const Volume& volume, glm::vec3 origin, glm::vec3 direction, float max_t) -> std::optional<VolumeHit>;

/** How many voxels hold each palette id */
[[nodiscard]]
TOAST_API auto idHistogram(const Volume& volume) -> std::array<uint32_t, k_palette_size>;

[[nodiscard]]
TOAST_API auto snapOrientation(const glm::mat3& rotation) noexcept -> LatticeOrientation;

[[nodiscard]]
TOAST_API auto dominantAxis(glm::vec3 normal) noexcept -> glm::ivec3;

}
