/**
 * @file voxel_volume.hpp
 * @author dario
 * @date 08/09/2026
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

	[[nodiscard]]
	auto entryAt(glm::ivec3 brick) const noexcept -> BrickEntry;

	[[nodiscard]]
	auto materialAt(glm::ivec3 voxel) const noexcept -> uint8_t;

	[[nodiscard]]
	auto isSolidAt(glm::ivec3 voxel) const noexcept -> bool;

	auto setVoxel(glm::ivec3 voxel, uint8_t material) -> VoxelWrite;

	void setBrickUniform(glm::ivec3 brick, uint8_t material);

	auto tryCollapseUniform(glm::ivec3 brick) -> bool;

	/// @returns false outside the volume or when the pool is exhausted
	auto setBrickMaterial(glm::ivec3 brick, std::span<const uint8_t, k_brick_material_bytes> material) -> bool;

	/// @brief Null when the brick holds nothing
	[[nodiscard]]
	auto occupancyPointer(glm::ivec3 brick) const noexcept -> const BrickOccupancy*;

	[[nodiscard]]
	auto neighbourhoodOf(glm::ivec3 brick) const noexcept -> BrickNeighbourhood;

	[[nodiscard]]
	auto solidVoxelCount() const -> uint32_t;

	[[nodiscard]]
	auto ownedBrickCount() const -> uint32_t;

	[[nodiscard]]
	auto sharedBrickCount() const -> uint32_t;

	[[nodiscard]]
	auto pool() const noexcept -> BrickPool* {
		return m_pool;
	}

private:
	[[nodiscard]]
	auto entryIndex(glm::ivec3 brick) const noexcept -> uint32_t;

	auto makeWritable(uint32_t entry_index) -> uint32_t;

	void releaseOwned();

	BrickPool* m_pool = nullptr;
	glm::uvec3 m_brick_dims {0};

	std::vector<BrickEntry> m_entries;
};

}
