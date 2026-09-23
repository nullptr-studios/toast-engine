#include "voxel_volume.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <utility>

namespace voxel {

namespace {

static_assert(k_brick_dim == 8, "brickOf and localOf shift and mask by 3 and 7");

[[nodiscard]]
auto brickOf(glm::ivec3 voxel) noexcept -> glm::ivec3 {
	return {voxel.x >> 3, voxel.y >> 3, voxel.z >> 3};
}

[[nodiscard]]
auto localOf(glm::ivec3 voxel) noexcept -> BrickCoord {
	return BrickCoord {static_cast<uint32_t>(voxel.x & 7), static_cast<uint32_t>(voxel.y & 7), static_cast<uint32_t>(voxel.z & 7)};
}

[[nodiscard]]
auto nextVolumeId() noexcept -> uint64_t {
	static std::atomic<uint64_t> counter {0};
	return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

}

Volume::Volume(BrickPool& pool, glm::uvec3 brick_dims) : m_pool(&pool), m_brick_dims(brick_dims), m_id(nextVolumeId()) {
	const size_t count = static_cast<size_t>(brick_dims.x) * brick_dims.y * brick_dims.z;
	m_entries.assign(count, BrickEntry {});
}

Volume::~Volume() {
	releaseOwned();
}

Volume::Volume(Volume&& other) noexcept
    : m_pool(other.m_pool),
      m_brick_dims(other.m_brick_dims),
      m_entries(std::move(other.m_entries)),
      m_revision(other.m_revision),
      m_id(other.m_id),
      m_dirty_mask(std::move(other.m_dirty_mask)),
      m_dirty_list(std::move(other.m_dirty_list)),
      m_dirty_since(other.m_dirty_since) {
	other.m_pool = nullptr;
	other.m_brick_dims = glm::uvec3 {0};
	other.m_entries.clear();
	other.m_dirty_mask.clear();
	other.m_dirty_list.clear();
	other.m_id = 0;
}

auto Volume::operator=(Volume&& other) noexcept -> Volume& {
	if (this != &other) {
		releaseOwned();
		m_pool = other.m_pool;
		m_brick_dims = other.m_brick_dims;
		m_entries = std::move(other.m_entries);
		m_revision = other.m_revision;
		m_id = other.m_id;
		m_dirty_mask = std::move(other.m_dirty_mask);
		m_dirty_list = std::move(other.m_dirty_list);
		m_dirty_since = other.m_dirty_since;
		other.m_pool = nullptr;
		other.m_brick_dims = glm::uvec3 {0};
		other.m_entries.clear();
		other.m_dirty_mask.clear();
		other.m_dirty_list.clear();
		other.m_id = 0;
	}
	return *this;
}

auto Volume::instanceOf(const Volume& source) -> Volume {
	assert(source.m_pool != nullptr);
	Volume out(*source.m_pool, source.m_brick_dims);

	out.m_entries = source.m_entries;
	for (BrickEntry& entry : out.m_entries) {
		if (entry.tag() == BrickTag::owned) {
			entry = BrickEntry::make(BrickTag::shared, entry.payload());
		}
	}
	return out;
}

void Volume::releaseOwned() {
	if (m_pool == nullptr) {
		return;
	}
	for (BrickEntry& entry : m_entries) {
		if (entry.tag() == BrickTag::owned) {
			m_pool->free(entry.payload());
			entry = BrickEntry {};
		}
	}
}

auto Volume::containsBrick(glm::ivec3 brick) const noexcept -> bool {
	return brick.x >= 0 && brick.y >= 0 && brick.z >= 0 && std::cmp_less(brick.x, m_brick_dims.x) &&
	       std::cmp_less(brick.y, m_brick_dims.y) && std::cmp_less(brick.z, m_brick_dims.z);
}

auto Volume::containsVoxel(glm::ivec3 voxel) const noexcept -> bool {
	return containsBrick(brickOf(voxel));
}

auto Volume::entryIndex(glm::ivec3 brick) const noexcept -> uint32_t {
	assert(containsBrick(brick));
	return static_cast<uint32_t>(brick.x) + (static_cast<uint32_t>(brick.y) * m_brick_dims.x) +
	       (static_cast<uint32_t>(brick.z) * m_brick_dims.x * m_brick_dims.y);
}

auto Volume::entryAt(glm::ivec3 brick) const noexcept -> BrickEntry {
	if (!containsBrick(brick)) {
		return BrickEntry {};
	}
	return m_entries[entryIndex(brick)];
}

auto Volume::materialAt(glm::ivec3 voxel) const noexcept -> uint8_t {
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

auto Volume::isSolidAt(glm::ivec3 voxel) const noexcept -> bool {
	return materialAt(voxel) != k_empty_palette_index;
}

auto Volume::occupancyPointer(glm::ivec3 brick) const noexcept -> const BrickOccupancy* {
	const BrickEntry entry = entryAt(brick);
	switch (entry.tag()) {
		case BrickTag::empty: return nullptr;
		case BrickTag::uniform: return &k_full_brick;
		default: return &m_pool->occupancy(entry.payload());
	}
}

auto Volume::neighbourhoodOf(glm::ivec3 brick) const noexcept -> BrickNeighbourhood {
	return BrickNeighbourhood {
	  occupancyPointer(brick + glm::ivec3 {-1, 0, 0}),
	  occupancyPointer(brick + glm::ivec3 {1, 0, 0}),
	  occupancyPointer(brick + glm::ivec3 {0, -1, 0}),
	  occupancyPointer(brick + glm::ivec3 {0, 1, 0}),
	  occupancyPointer(brick + glm::ivec3 {0, 0, -1}),
	  occupancyPointer(brick + glm::ivec3 {0, 0, 1}),
	};
}

auto Volume::makeWritable(uint32_t entry_index) -> uint32_t {
	const BrickEntry entry = m_entries[entry_index];
	if (entry.tag() == BrickTag::owned) {
		return entry.payload();
	}

	const uint32_t id = m_pool->allocate();
	if (id == k_invalid_brick) {
		return k_invalid_brick;
	}

	switch (entry.tag()) {
		case BrickTag::uniform: {
			const auto fill = static_cast<uint8_t>(entry.payload());
			std::span<uint8_t, k_brick_material_bytes> bytes = m_pool->material(id);
			std::fill(bytes.begin(), bytes.end(), fill);
			m_pool->occupancy(id) = k_full_brick;
			break;
		}
		case BrickTag::shared: {
			const uint32_t source_id = entry.payload();
			std::span<const uint8_t, k_brick_material_bytes> source_bytes = m_pool->material(source_id);
			std::span<uint8_t, k_brick_material_bytes> bytes = m_pool->material(id);
			std::copy(source_bytes.begin(), source_bytes.end(), bytes.begin());
			m_pool->occupancy(id) = m_pool->occupancy(source_id);
			break;
		}
		default: break;
	}

	m_entries[entry_index] = BrickEntry::make(BrickTag::owned, id);
	return id;
}

auto Volume::setVoxel(glm::ivec3 voxel, uint8_t material) -> VoxelWrite {
	VoxelWrite result;
	if (!containsVoxel(voxel)) {
		return result;
	}

	const uint8_t previous = materialAt(voxel);
	result.previous_material = previous;
	if (previous == material) {
		return result;
	}

	const uint32_t index = entryIndex(brickOf(voxel));
	const bool was_occupied = m_entries[index].tag() != BrickTag::empty;

	const uint32_t id = makeWritable(index);
	if (id == k_invalid_brick) {
		return result;
	}

	const BrickCoord local = localOf(voxel);
	m_pool->material(id)[localIndex(local.x, local.y, local.z)] = material;
	setSolid(m_pool->occupancy(id), local.x, local.y, local.z, material != k_empty_palette_index);

	result.changed = true;
	++m_revision;
	markDirty(brickOf(voxel));

	if (material != k_empty_palette_index) {
		result.brick_became_occupied = !was_occupied;
	} else if (isEmpty(m_pool->occupancy(id))) {
		m_pool->free(id);
		m_entries[index] = BrickEntry {};
		result.brick_became_empty = true;
	}

	return result;
}

void Volume::setBrickUniform(glm::ivec3 brick, uint8_t material) {
	if (!containsBrick(brick)) {
		return;
	}

	const uint32_t index = entryIndex(brick);
	const BrickEntry next = material == k_empty_palette_index ? BrickEntry {} : BrickEntry::make(BrickTag::uniform, material);
	if (m_entries[index] == next) {
		return;
	}
	if (m_entries[index].tag() == BrickTag::owned) {
		m_pool->free(m_entries[index].payload());
	}

	m_entries[index] = next;
	++m_revision;
	markDirty(brick);
}

auto Volume::tryCollapseUniform(glm::ivec3 brick) -> bool {
	if (!containsBrick(brick)) {
		return false;
	}

	const uint32_t index = entryIndex(brick);
	const BrickEntry entry = m_entries[index];
	if (entry.tag() != BrickTag::owned) {
		return false;
	}

	const uint32_t id = entry.payload();
	if (!isFull(m_pool->occupancy(id))) {
		return false;
	}

	std::span<const uint8_t, k_brick_material_bytes> bytes = m_pool->material(id);
	const uint8_t first = bytes[0];
	if (first == k_empty_palette_index) {
		return false;
	}
	if (std::any_of(bytes.begin(), bytes.end(), [first](uint8_t value) { return value != first; })) {
		return false;
	}

	m_pool->free(id);
	m_entries[index] = BrickEntry::make(BrickTag::uniform, first);
	++m_revision;
	markDirty(brick);
	return true;
}

auto Volume::setBrickMaterial(glm::ivec3 brick, std::span<const uint8_t, k_brick_material_bytes> material) -> bool {
	if (!containsBrick(brick)) {
		return false;
	}

	const uint8_t first = material[0];
	if (std::all_of(material.begin(), material.end(), [first](uint8_t value) { return value == first; })) {
		setBrickUniform(brick, first);
		return true;
	}

	const uint32_t id = makeWritable(entryIndex(brick));
	if (id == k_invalid_brick) {
		return false;
	}

	std::copy(material.begin(), material.end(), m_pool->material(id).begin());

	BrickOccupancy occupancy {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		uint64_t word = 0;
		for (uint32_t bit = 0; bit < 64; ++bit) {
			if (material[(z * 64) + bit] != k_empty_palette_index) {
				word |= 1ull << bit;
			}
		}
		occupancy[z] = word;
	}
	m_pool->occupancy(id) = occupancy;
	++m_revision;
	markDirty(brick);
	return true;
}

void Volume::markDirty(glm::ivec3 brick) {
	const uint32_t index = entryIndex(brick);
	if (m_dirty_mask.empty()) {
		m_dirty_mask.assign((m_entries.size() + 63) / 64, 0);
	}

	uint64_t& word = m_dirty_mask[index >> 6];
	const uint64_t bit = 1ull << (index & 63);
	if ((word & bit) == 0) {
		word |= bit;
		m_dirty_list.push_back(index);
	}
}

auto Volume::dirtyBricksSince(uint32_t revision) const noexcept -> std::optional<std::span<const uint32_t>> {
	if (revision < m_dirty_since || revision > m_revision) {
		return std::nullopt;
	}
	return std::span<const uint32_t> {m_dirty_list};
}

void Volume::discardDirty() const noexcept {
	for (const uint32_t index : m_dirty_list) {
		m_dirty_mask[index >> 6] &= ~(1ull << (index & 63));
	}
	m_dirty_list.clear();
	m_dirty_since = m_revision;
}

auto Volume::brickAtIndex(uint32_t index) const noexcept -> glm::ivec3 {
	const uint32_t layer = m_brick_dims.x * m_brick_dims.y;
	return glm::ivec3(
	    static_cast<int>(index % m_brick_dims.x),
	    static_cast<int>((index / m_brick_dims.x) % m_brick_dims.y),
	    static_cast<int>(index / layer)
	);
}

auto Volume::solidVoxelCount() const -> uint32_t {
	uint32_t total = 0;
	for (const BrickEntry& entry : m_entries) {
		switch (entry.tag()) {
			case BrickTag::empty: break;
			case BrickTag::uniform: total += k_brick_voxel_count; break;
			default: total += popCount(m_pool->occupancy(entry.payload())); break;
		}
	}
	return total;
}

auto Volume::ownedBrickCount() const -> uint32_t {
	return static_cast<uint32_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const BrickEntry& entry) {
		return entry.tag() == BrickTag::owned;
	}));
}

auto Volume::sharedBrickCount() const -> uint32_t {
	return static_cast<uint32_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const BrickEntry& entry) {
		return entry.tag() == BrickTag::shared;
	}));
}

}
