/**
 * @file brick_pool.hpp
 * @author dario
 * @date 08/09/2026
 */

#pragma once
#include "brick.hpp"
#include "voxel_constants.hpp"

#include <bit>
#include <cstdint>
#include <span>
#include <toast/export.hpp>
#include <vector>

namespace voxel {

inline constexpr size_t k_brick_material_bytes = static_cast<size_t>(k_brick_voxel_count);

inline constexpr uint32_t k_invalid_brick = k_brick_payload_mask;

/// @note Not thread safe
class TOAST_API BrickPool {
public:
	explicit BrickPool(uint32_t capacity);

	/// @returns k_invalid_brick when the pool is exhausted
	[[nodiscard]]
	auto allocate() -> uint32_t;

	/// @brief The id must come from allocate and not already be freed
	void free(uint32_t id);

	[[nodiscard]]
	auto material(uint32_t id) -> std::span<uint8_t, k_brick_material_bytes>;

	[[nodiscard]]
	auto material(uint32_t id) const -> std::span<const uint8_t, k_brick_material_bytes>;

	[[nodiscard]]
	auto occupancy(uint32_t id) -> BrickOccupancy&;

	[[nodiscard]]
	auto occupancy(uint32_t id) const -> const BrickOccupancy&;

	[[nodiscard]]
	auto capacity() const noexcept -> uint32_t {
		return m_capacity;
	}

	[[nodiscard]]
	auto allocatedCount() const noexcept -> uint32_t {
		return m_next_unused - static_cast<uint32_t>(m_free_list.size());
	}

	[[nodiscard]]
	auto freeCount() const noexcept -> uint32_t {
		return m_capacity - allocatedCount();
	}

	[[nodiscard]]
	auto isValid(uint32_t id) const noexcept -> bool {
		return id < m_next_unused;
	}

private:
	void clearBrick(uint32_t id);

	uint32_t m_capacity = 0;

	uint32_t m_next_unused = 0;

	std::vector<uint32_t> m_free_list;

	std::vector<uint8_t> m_material;
	std::vector<BrickOccupancy> m_occupancy;
};

static_assert(std::endian::native == std::endian::little, "the brick material layer assumes a little-endian host");

}
