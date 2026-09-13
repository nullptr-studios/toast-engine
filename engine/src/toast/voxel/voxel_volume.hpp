/**
 * @file volume.hpp
 * @author dario
 * @date 08/09/2026
 *
 * @brief an indirection grid over pooled bricks
 *
 * A volume is the collision shape and the render primitive
 */

#pragma once
#include "brick.hpp"
#include "brick_pool.hpp"
#include "voxel_constants.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <toast/export.hpp>
#include <vector>

namespace toast::voxel {

class TOAST_API Volume {
public:
	/**
	 * @brief What one voxel write changed
	 */
	struct VoxelWrite {
		uint8_t previous_material = k_empty_palette_index;

		/// False when the write was a no-op
		bool changed = false;

		/// The brick held nothing and now holds something
		bool brick_became_occupied = false;

		/// The brick held something and now holds nothing
		bool brick_became_empty = false;
	};

	/// @brief An empty volume of @p brick_dims bricks, drawing storage from @p pool
	Volume(BrickPool& pool, glm::uvec3 brick_dims);

	~Volume();

	Volume(const Volume&) = delete;
	auto operator=(const Volume&) -> Volume& = delete;
	Volume(Volume&& other) noexcept;
	auto operator=(Volume&& other) noexcept -> Volume&;

	/**
	 * @brief A writable instance sharing every brick with @p source until it is written to
	 */
	[[nodiscard]]
	static auto instanceOf(const Volume& source) -> Volume;

	[[nodiscard]]
	auto brickDims() const noexcept -> glm::uvec3 {
		return m_brick_dims;
	}

	[[nodiscard]]
	auto voxelDims() const noexcept -> glm::uvec3 {
		return m_brick_dims * k_brick_dim;
	}

	[[nodiscard]]
	auto brickCount() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_entries.size());
	}

	[[nodiscard]]
	auto containsBrick(glm::ivec3 brick) const noexcept -> bool;

	[[nodiscard]]
	auto containsVoxel(glm::ivec3 voxel) const noexcept -> bool;

	/// @returns the indirection entry
	[[nodiscard]]
	auto entryAt(glm::ivec3 brick) const noexcept -> BrickEntry;

	/// @returns the palette index at @p voxel
	[[nodiscard]]
	auto materialAt(glm::ivec3 voxel) const noexcept -> uint8_t;

	[[nodiscard]]
	auto isSolidAt(glm::ivec3 voxel) const noexcept -> bool;

	/**
	 * @brief Writes one voxel, materialising and releasing bricks as needed
	 */
	auto setVoxel(glm::ivec3 voxel, uint8_t material) -> VoxelWrite;

	/**
	 * @brief Makes a whole brick solid with one material
	 */
	void setBrickUniform(glm::ivec3 brick, uint8_t material);

	/**
	 * @brief Collapses a pooled brick back to a uniform entry when every voxel shares one material
	 *
	 * @returns true when the brick was collapsed
	 */
	auto tryCollapseUniform(glm::ivec3 brick) -> bool;

	/**
	 * @brief The occupancy of one brick, or null when it holds nothing
	 */
	[[nodiscard]]
	auto occupancyPointer(glm::ivec3 brick) const noexcept -> const BrickOccupancy*;

	/// @brief The six bricks abutting @p brick
	[[nodiscard]]
	auto neighbourhoodOf(glm::ivec3 brick) const noexcept -> BrickNeighbourhood;

	[[nodiscard]]
	auto solidVoxelCount() const -> uint32_t;

	/// @brief Bricks this volume owns outright
	[[nodiscard]]
	auto ownedBrickCount() const -> uint32_t;

	/// @brief Bricks still shared with the source this volume was instantiated from
	[[nodiscard]]
	auto sharedBrickCount() const -> uint32_t;

	[[nodiscard]]
	auto pool() const noexcept -> BrickPool* {
		return m_pool;
	}

private:
	[[nodiscard]]
	auto entryIndex(glm::ivec3 brick) const noexcept -> uint32_t;

	/// @brief Ensures the entry at @p entry_index owns writable storage
	auto makeWritable(uint32_t entry_index) -> uint32_t;

	void releaseOwned();

	BrickPool* m_pool = nullptr;
	glm::uvec3 m_brick_dims {0};

	std::vector<BrickEntry> m_entries;
};

}
