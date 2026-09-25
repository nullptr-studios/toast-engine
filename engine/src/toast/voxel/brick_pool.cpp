#include "brick_pool.hpp"

#include <algorithm>
#include <cassert>

namespace voxel {

namespace {

[[nodiscard]]
constexpr auto packHead(uint32_t id, uint32_t tag) noexcept -> uint64_t {
	return (static_cast<uint64_t>(tag) << 32) | id;
}

[[nodiscard]]
constexpr auto headId(uint64_t head) noexcept -> uint32_t {
	return static_cast<uint32_t>(head);
}

[[nodiscard]]
constexpr auto headTag(uint64_t head) noexcept -> uint32_t {
	return static_cast<uint32_t>(head >> 32);
}

}

BrickPool::BrickPool(uint32_t capacity)
    : m_capacity(capacity),
      m_free_head(packHead(k_invalid_brick, 0)),
      m_next_free(std::make_unique<std::atomic<uint32_t>[]>(capacity)),
      m_is_free(std::make_unique<std::atomic<bool>[]>(capacity)) {
	assert(capacity < k_invalid_brick);

	m_material.assign(static_cast<size_t>(capacity) * k_brick_material_bytes, k_empty_palette_index);
	m_occupancy.assign(capacity, BrickOccupancy {});
}

auto BrickPool::allocate() -> uint32_t {
	uint64_t head = m_free_head.load(std::memory_order_acquire);
	while (headId(head) != k_invalid_brick) {
		const uint32_t id = headId(head);
		const uint32_t next = m_next_free[id].load(std::memory_order_relaxed);
		if (m_free_head.compare_exchange_weak(head, packHead(next, headTag(head) + 1), std::memory_order_acquire)) {
			m_is_free[id].store(false, std::memory_order_relaxed);
			m_allocated.fetch_add(1, std::memory_order_relaxed);
			clearBrick(id);
			return id;
		}
	}

	uint32_t unused = m_next_unused.load(std::memory_order_relaxed);
	do {
		if (unused >= m_capacity) {
			return k_invalid_brick;
		}
	} while (!m_next_unused.compare_exchange_weak(unused, unused + 1, std::memory_order_acq_rel));

	m_allocated.fetch_add(1, std::memory_order_relaxed);
	clearBrick(unused);
	return unused;
}

void BrickPool::free(uint32_t id) {
	assert(isValid(id));
	assert(!m_is_free[id].exchange(true, std::memory_order_relaxed) && "brick freed twice");

	uint64_t head = m_free_head.load(std::memory_order_relaxed);
	do {
		m_next_free[id].store(headId(head), std::memory_order_relaxed);
	} while (!m_free_head.compare_exchange_weak(head, packHead(id, headTag(head) + 1), std::memory_order_release));
	m_allocated.fetch_sub(1, std::memory_order_relaxed);
}

void BrickPool::clearBrick(uint32_t id) {
	assert(id < m_capacity);
	const size_t base = static_cast<size_t>(id) * k_brick_material_bytes;
	std::fill_n(m_material.begin() + static_cast<std::ptrdiff_t>(base), k_brick_material_bytes, k_empty_palette_index);
	m_occupancy[id] = BrickOccupancy {};
}

auto BrickPool::material(uint32_t id) -> std::span<uint8_t, k_brick_material_bytes> {
	assert(isValid(id));
	const size_t base = static_cast<size_t>(id) * k_brick_material_bytes;
	return std::span<uint8_t, k_brick_material_bytes>(m_material.data() + base, k_brick_material_bytes);
}

auto BrickPool::material(uint32_t id) const -> std::span<const uint8_t, k_brick_material_bytes> {
	assert(isValid(id));
	const size_t base = static_cast<size_t>(id) * k_brick_material_bytes;
	return std::span<const uint8_t, k_brick_material_bytes>(m_material.data() + base, k_brick_material_bytes);
}

auto BrickPool::occupancy(uint32_t id) -> BrickOccupancy& {
	assert(isValid(id));
	return m_occupancy[id];
}

auto BrickPool::occupancy(uint32_t id) const -> const BrickOccupancy& {
	assert(isValid(id));
	return m_occupancy[id];
}

}
