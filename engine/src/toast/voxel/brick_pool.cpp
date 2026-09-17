#include "brick_pool.hpp"

#include <algorithm>
#include <cassert>

namespace toast::voxel {

BrickPool::BrickPool(uint32_t capacity) : m_capacity(capacity) {
	assert(capacity < k_invalid_brick);

	m_material.assign(static_cast<size_t>(capacity) * k_brick_material_bytes, k_empty_palette_index);
	m_occupancy.assign(capacity, BrickOccupancy {});
	m_free_list.reserve(64);
}

auto BrickPool::allocate() -> uint32_t {
	if (!m_free_list.empty()) {
		const uint32_t id = m_free_list.back();
		m_free_list.pop_back();
		clearBrick(id);
		return id;
	}

	if (m_next_unused >= m_capacity) {
		return k_invalid_brick;
	}

	const uint32_t id = m_next_unused++;
	clearBrick(id);
	return id;
}

void BrickPool::free(uint32_t id) {
	assert(isValid(id));
	assert(std::find(m_free_list.begin(), m_free_list.end(), id) == m_free_list.end() && "brick freed twice");
	m_free_list.push_back(id);
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
