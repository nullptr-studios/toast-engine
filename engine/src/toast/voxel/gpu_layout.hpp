/**
 * @file gpu_layout.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "brick_pool.hpp"
#include "palette.hpp"
#include "voxel_constants.hpp"
#include "voxel_volume.hpp"

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <toast/export.hpp>
#include <vector>

namespace voxel::gpu {

inline constexpr uint32_t k_material_words_per_brick = k_brick_voxel_count / 4;

inline constexpr uint32_t k_occupancy_words_per_brick = 16;

inline constexpr uint32_t k_coarse_bricks = 4;

inline constexpr uint32_t k_palette_words = k_palette_size * 4;

static_assert(k_brick_voxel_count % 4 == 0, "brick material bytes must pack into whole 32-bit words");
static_assert(sizeof(BrickOccupancy) == 64, "occupancy must be eight 64-bit slices");
static_assert(sizeof(PaletteEntry) == 16, "a palette entry must be four 32-bit words");

/// @brief Mirrored member for member by voxel_dda.slang so only 4-byte scalars
struct VolumeRecord {
	uint32_t brick_dims_x = 0;
	uint32_t brick_dims_y = 0;
	uint32_t brick_dims_z = 0;

	/// In entries
	uint32_t grid_offset = 0;

	uint32_t coarse_dims_x = 0;
	uint32_t coarse_dims_y = 0;
	uint32_t coarse_dims_z = 0;

	/// In words
	uint32_t coarse_offset = 0;

	/// In entries so multiply by four for words
	uint32_t palette_offset = 0;

	float max_emissive = 1.0f;
};

static_assert(sizeof(VolumeRecord) == 40, "VolumeRecord is mirrored by voxel_dda.slang member for member");
static_assert(offsetof(VolumeRecord, grid_offset) == 12);
static_assert(offsetof(VolumeRecord, coarse_offset) == 28);
static_assert(offsetof(VolumeRecord, palette_offset) == 32);
static_assert(offsetof(VolumeRecord, max_emissive) == 36);

struct PackedPool {
	/// 512 palette bytes per slot little endian
	std::vector<uint32_t> materials;

	/// Eight 64-bit z-slices per slot low word first
	std::vector<uint32_t> occupancy;
};

/// @brief Neither pointer may be null
struct SceneVolume {
	const Volume* volume = nullptr;
	const Palette* palette = nullptr;
};

struct PackedScene {
	std::vector<VolumeRecord> records;

	/// Concatenated per volume one BrickEntry per brick x fastest
	std::vector<uint32_t> grids;

	/// Concatenated per volume one bit per 4x4x4 bricks
	std::vector<uint32_t> coarse;

	std::vector<uint32_t> palettes;
};

/// A pack minus the per brick arrays
struct SceneLayout {
	std::vector<VolumeRecord> records;
	std::vector<uint32_t> palettes;
	uint32_t grid_words = 0;
	uint32_t coarse_words = 0;
};

/// Words replaced in one section of the copy the GPU holds
struct PatchRun {
	uint32_t dst = 0;
	uint32_t count = 0;
	uint32_t src = 0;
};

struct PatchSection {
	std::vector<PatchRun> runs;

	/// The runs back to back
	std::vector<uint32_t> words;
};

struct TagDelta {
	int32_t uniform = 0;
	int32_t shared = 0;
	int32_t owned = 0;
};

struct ScenePatch {
	/// Slots the pool sections hold afterwards and they only ever grow
	uint32_t pool_slots = 0;

	PatchSection materials;
	PatchSection occupancy;
	PatchSection grids;
	PatchSection coarse;

	/// Per scene volume
	std::vector<TagDelta> tags;
};

/// Grid indices of the bricks of one scene volume that changed since the retained state
struct VolumeDirty {
	uint32_t volume = 0;
	std::span<const uint32_t> bricks;
};

[[nodiscard]]
constexpr auto gridIndex(glm::uvec3 dims, glm::uvec3 brick) noexcept -> uint32_t {
	return brick.x + brick.y * dims.x + brick.z * dims.x * dims.y;
}

[[nodiscard]]
constexpr auto coarseDims(glm::uvec3 brick_dims) noexcept -> glm::uvec3 {
	return (brick_dims + (k_coarse_bricks - 1u)) / k_coarse_bricks;
}

/// @brief Packs up to the highest slot ever handed out
[[nodiscard]]
TOAST_API auto packPool(const BrickPool& pool) -> PackedPool;

[[nodiscard]]
TOAST_API auto packScene(std::span<const SceneVolume> volumes) -> PackedScene;

[[nodiscard]]
TOAST_API auto packLayout(std::span<const SceneVolume> volumes) -> SceneLayout;

/// A patch only stands in for a full pack while this holds
[[nodiscard]]
TOAST_API auto layoutMatches(const PackedScene& retained, const SceneLayout& layout) -> bool;

/// @pre layoutMatches and leaves retained grids and coarse as the GPU holds them after the patch
[[nodiscard]]
TOAST_API auto computePatch(
    PackedScene& retained, uint32_t retained_pool_slots, const BrickPool& pool, std::span<const SceneVolume> volumes,
    std::span<const VolumeDirty> dirty
) -> ScenePatch;

/// Applies a patch the way the GPU copies do
TOAST_API void applyPatch(PackedPool& pool, PackedScene& scene, const ScenePatch& patch);

[[nodiscard]]
TOAST_API auto patchWords(const ScenePatch& patch) -> size_t;

/// @brief The sample functions mirror the voxel_dda.slang fetches
[[nodiscard]]
TOAST_API auto sampleMaterial(const PackedPool& pool, const PackedScene& scene, uint32_t volume, glm::ivec3 voxel) -> uint8_t;

[[nodiscard]]
TOAST_API auto sampleSolid(const PackedPool& pool, const PackedScene& scene, uint32_t volume, glm::ivec3 voxel) -> bool;

[[nodiscard]]
TOAST_API auto sampleCoarse(const PackedScene& scene, uint32_t volume, glm::ivec3 cell) -> bool;

[[nodiscard]]
TOAST_API auto samplePaletteEntry(const PackedScene& scene, uint32_t volume, uint8_t index) -> PaletteEntry;

}
