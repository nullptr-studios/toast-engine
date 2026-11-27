/**
 * @file voxel_volume.hpp
 * @author dario
 * @date 08/09/2026
 */

#pragma once
#include "brick.hpp"
#include "brick_pool.hpp"
#include "voxel_constants.hpp"

#include <cassert>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <toast/export.hpp>
#include <utility>
#include <vector>

namespace voxel {

static_assert(k_brick_dim == 8, "brickOf and localOf shift and mask by 3 and 7");

/// @brief Arithmetic shift so voxel -1 lands in brick -1
[[nodiscard]]
inline auto brickOf(glm::ivec3 voxel) noexcept -> glm::ivec3 {
	return {voxel.x >> 3, voxel.y >> 3, voxel.z >> 3};
}

[[nodiscard]]
inline auto localOf(glm::ivec3 voxel) noexcept -> BrickCoord {
	return BrickCoord {static_cast<uint32_t>(voxel.x & 7), static_cast<uint32_t>(voxel.y & 7), static_cast<uint32_t>(voxel.z & 7)};
}

class TOAST_API Volume {
public:
	struct VoxelWrite {
		uint8_t previous_material = k_empty_palette_index;

		bool changed = false;

		bool brick_became_occupied = false;

		bool brick_became_empty = false;
	};

	Volume(BrickPool& pool, glm::uvec3 brick_dims);

	~Volume();

	Volume(const Volume&) = delete;
	auto operator=(const Volume&) -> Volume& = delete;
	Volume(Volume&& other) noexcept;
	auto operator=(Volume&& other) noexcept -> Volume&;

	/// @brief Shares every brick with @p source until written
	[[nodiscard]]
	static auto instanceOf(const Volume& source) -> Volume;

	/// @brief Same result as assigning instanceOf(source) but only retains and frees the bricks that differ
	void reshareFrom(const Volume& source);

	[[nodiscard]]
	static auto adoptResized(Volume& source, glm::ivec3 brick_offset, glm::uvec3 brick_dims) -> Volume;

	[[nodiscard]]
	auto brickDims() const noexcept -> glm::uvec3 {
		return m_brick_dims;
	}

	/// Voxel coordinate plus this is where the detail texture reads so a fragment keeps the pattern of its source
	[[nodiscard]]
	auto textureOrigin() const noexcept -> glm::ivec3 {
		return m_texture_origin;
	}

	void setTextureOrigin(glm::ivec3 origin) noexcept { m_texture_origin = origin; }

	[[nodiscard]]
	auto voxelDims() const noexcept -> glm::uvec3 {
		return m_brick_dims * k_brick_dim;
	}

	[[nodiscard]]
	auto brickCount() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_entries.size());
	}

	[[nodiscard]]
	auto containsBrick(glm::ivec3 brick) const noexcept -> bool {
		return brick.x >= 0 && brick.y >= 0 && brick.z >= 0 && std::cmp_less(brick.x, m_brick_dims.x) &&
		       std::cmp_less(brick.y, m_brick_dims.y) && std::cmp_less(brick.z, m_brick_dims.z);
	}

	[[nodiscard]]
	auto containsVoxel(glm::ivec3 voxel) const noexcept -> bool {
		return containsBrick(brickOf(voxel));
	}

	[[nodiscard]]
	auto entryAt(glm::ivec3 brick) const noexcept -> BrickEntry {
		if (!containsBrick(brick)) {
			return BrickEntry {};
		}
		return m_entries[entryIndex(brick)];
	}

	[[nodiscard]]
	auto materialAt(glm::ivec3 voxel) const noexcept -> uint8_t {
		if (!containsVoxel(voxel)) {
			return k_empty_palette_index;
		}

		const BrickEntry entry = m_entries[entryIndex(brickOf(voxel))];
		switch (entry.tag()) {
			case BrickTag::empty: return k_empty_palette_index;
			case BrickTag::uniform: return static_cast<uint8_t>(entry.payload());
			default: break;
		}
		const BrickCoord local = localOf(voxel);
		return m_pool->material(entry.payload())[localIndex(local.x, local.y, local.z)];
	}

	[[nodiscard]]
	auto isSolidAt(glm::ivec3 voxel) const noexcept -> bool {
		return materialAt(voxel) != k_empty_palette_index;
	}

	auto setVoxel(glm::ivec3 voxel, uint8_t material) -> VoxelWrite;

	void setBrickUniform(glm::ivec3 brick, uint8_t material);

	auto tryCollapseUniform(glm::ivec3 brick) -> bool;

	/// @returns false outside the volume or when the pool is exhausted
	auto setBrickMaterial(glm::ivec3 brick, std::span<const uint8_t, k_brick_material_bytes> material) -> bool;

	/// Caller must adopt or free a pooled entry
	[[nodiscard]]
	auto takeBrick(glm::ivec3 brick) -> BrickEntry;

	/// Slot must be empty and a pooled entry must be owned
	void adoptBrick(glm::ivec3 brick, BrickEntry entry);

	/// @returns false with nothing changed when the pool has no brick to copy into
	auto clearVoxels(glm::ivec3 brick, const BrickOccupancy& mask) -> bool;

	/**
	 * Makes brick hold what source_brick of source holds
	 * @returns true when the brick changed
	 */
	auto copyBrickFrom(glm::ivec3 brick, const Volume& source, glm::ivec3 source_brick) -> bool;

	struct WritableBrick {
		std::span<uint8_t, k_brick_material_bytes> material;
		BrickOccupancy* occupancy = nullptr;
	};

	auto beginBrickWrite(glm::ivec3 brick) -> std::optional<WritableBrick>;
	void finishBrickWrite(glm::ivec3 brick, bool changed);

	/// @brief Null when the brick holds nothing
	[[nodiscard]]
	auto occupancyPointer(glm::ivec3 brick) const noexcept -> const BrickOccupancy* {
		const BrickEntry entry = entryAt(brick);
		switch (entry.tag()) {
			case BrickTag::empty: return nullptr;
			case BrickTag::uniform: return &k_full_brick;
			default: return &std::as_const(*m_pool).occupancy(entry.payload());
		}
	}

	[[nodiscard]]
	auto neighbourhoodOf(glm::ivec3 brick) const noexcept -> BrickNeighbourhood;

	[[nodiscard]]
	auto solidVoxelCount() const noexcept -> uint32_t {
		return m_solid_voxels;
	}

	/// Solid voxels of one brick, 0 outside the volume
	[[nodiscard]]
	auto brickSolidCount(glm::ivec3 brick) const noexcept -> uint32_t {
		return containsBrick(brick) ? m_brick_solid[entryIndex(brick)] : 0u;
	}

	/**
	 * Calls visit(brick, slot) for every brick that holds a solid voxel in slot order, which is z then y then x
	 * @note Skips empty space 64 bricks at a time so a sparse volume costs what is in it
	 */
	template<typename Visit>
	void forEachSolidBrick(Visit&& visit) const {
		for (size_t word_index = 0; word_index < m_solid_mask.size(); ++word_index) {
			uint64_t word = m_solid_mask[word_index];
			while (word != 0ull) {
				const auto slot = static_cast<uint32_t>((word_index << 6u) + static_cast<size_t>(std::countr_zero(word)));
				word &= word - 1ull;
				visit(brickAtIndex(slot), slot);
			}
		}
	}

	[[nodiscard]]
	auto ownedBrickCount() const -> uint32_t;

	[[nodiscard]]
	auto sharedBrickCount() const -> uint32_t;

	/// @brief Bumped by writes that change a voxel and by a collapse since that changes the packed form
	[[nodiscard]]
	auto revision() const noexcept -> uint32_t {
		return m_revision;
	}

	[[nodiscard]]
	auto pool() const noexcept -> BrickPool* {
		return m_pool;
	}

	/// Unique to this content so a replaced volume never matches the id of the one before it
	[[nodiscard]]
	auto id() const noexcept -> uint64_t {
		return m_id;
	}

	/// Nullopt once the history from @p revision was discarded
	[[nodiscard]]
	auto dirtyBricksSince(uint32_t revision) const noexcept -> std::optional<std::span<const uint32_t>>;

	[[nodiscard]]
	auto brickAtIndex(uint32_t index) const noexcept -> glm::ivec3;

	/// Forgets the history up to now and only the renderer reads it
	void discardDirty() const noexcept;

private:
	void markDirty(glm::ivec3 brick);

	[[nodiscard]]
	auto entryIndex(glm::ivec3 brick) const noexcept -> uint32_t {
		assert(containsBrick(brick));
		return static_cast<uint32_t>(brick.x) + (static_cast<uint32_t>(brick.y) * m_brick_dims.x) +
		       (static_cast<uint32_t>(brick.z) * m_brick_dims.x * m_brick_dims.y);
	}

	auto makeWritable(uint32_t entry_index) -> uint32_t;

	void releaseOwned();

	void syncBrick(uint32_t index) noexcept;

	void recountAll() noexcept;

	BrickPool* m_pool = nullptr;
	glm::uvec3 m_brick_dims {0};

	glm::ivec3 m_texture_origin {0};
	std::vector<BrickEntry> m_entries;

	std::vector<uint16_t> m_brick_solid;
	/// One bit per slot that holds a solid voxel
	std::vector<uint64_t> m_solid_mask;
	uint32_t m_solid_voxels = 0;

	uint32_t m_revision = 0;

	uint64_t m_id = 0;

	/// Bookkeeping for the renderer so reading it never counts as a change
	mutable std::vector<uint64_t> m_dirty_mask;
	mutable std::vector<uint32_t> m_dirty_list;
	mutable uint32_t m_dirty_since = 0;
};

}
