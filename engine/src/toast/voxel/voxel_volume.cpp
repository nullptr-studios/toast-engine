#include "voxel_volume.hpp"

#include <cassert>

namespace voxel {

namespace {

[[nodiscard]]
auto nextVolumeId() noexcept -> uint64_t {
	static std::atomic<uint64_t> counter {0};
	return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

}

Volume::Volume(BrickPool& pool, glm::uvec3 brick_dims) : m_pool(&pool), m_brick_dims(brick_dims), m_id(nextVolumeId()) {
	const size_t count = static_cast<size_t>(brick_dims.x) * brick_dims.y * brick_dims.z;
	m_entries.assign(count, BrickEntry {});
	m_brick_solid.assign(count, 0);
	m_solid_mask.assign((count + 63) / 64, 0ull);
}

Volume::~Volume() {
	releaseOwned();
}

Volume::Volume(Volume&& other) noexcept
    : m_pool(other.m_pool),
      m_brick_dims(other.m_brick_dims),
      m_texture_origin(other.m_texture_origin),
      m_entries(std::move(other.m_entries)),
      m_brick_solid(std::move(other.m_brick_solid)),
      m_solid_mask(std::move(other.m_solid_mask)),
      m_solid_voxels(other.m_solid_voxels),
      m_revision(other.m_revision),
      m_id(other.m_id),
      m_dirty_mask(std::move(other.m_dirty_mask)),
      m_dirty_list(std::move(other.m_dirty_list)),
      m_dirty_since(other.m_dirty_since) {
	other.m_pool = nullptr;
	other.m_brick_dims = glm::uvec3 {0};
	other.m_entries.clear();
	other.m_brick_solid.clear();
	other.m_solid_mask.clear();
	other.m_solid_voxels = 0;
	other.m_dirty_mask.clear();
	other.m_dirty_list.clear();
	other.m_id = 0;
}

auto Volume::operator=(Volume&& other) noexcept -> Volume& {
	if (this != &other) {
		releaseOwned();
		m_pool = other.m_pool;
		m_brick_dims = other.m_brick_dims;
		m_texture_origin = other.m_texture_origin;
		m_entries = std::move(other.m_entries);
		m_brick_solid = std::move(other.m_brick_solid);
		m_solid_mask = std::move(other.m_solid_mask);
		m_solid_voxels = other.m_solid_voxels;
		m_revision = other.m_revision;
		m_id = other.m_id;
		m_dirty_mask = std::move(other.m_dirty_mask);
		m_dirty_list = std::move(other.m_dirty_list);
		m_dirty_since = other.m_dirty_since;
		other.m_pool = nullptr;
		other.m_brick_dims = glm::uvec3 {0};
		other.m_entries.clear();
		other.m_brick_solid.clear();
		other.m_solid_mask.clear();
		other.m_solid_voxels = 0;
		other.m_dirty_mask.clear();
		other.m_dirty_list.clear();
		other.m_id = 0;
	}
	return *this;
}

void Volume::syncBrick(uint32_t index) noexcept {
	const BrickEntry entry = m_entries[index];
	uint32_t count = 0;
	switch (entry.tag()) {
		case BrickTag::empty: break;
		case BrickTag::uniform: count = k_brick_voxel_count; break;
		default: count = popCount(std::as_const(*m_pool).occupancy(entry.payload())); break;
	}
	m_solid_voxels = m_solid_voxels - m_brick_solid[index] + count;
	m_brick_solid[index] = static_cast<uint16_t>(count);
	uint64_t& word = m_solid_mask[index >> 6u];
	const uint64_t bit = 1ull << (index & 63u);
	word = count != 0 ? (word | bit) : (word & ~bit);
}

void Volume::recountAll() noexcept {
	m_brick_solid.assign(m_entries.size(), 0);
	m_solid_mask.assign((m_entries.size() + 63) / 64, 0ull);
	m_solid_voxels = 0;
	for (uint32_t index = 0; index < m_entries.size(); ++index) {
		if (m_entries[index].tag() != BrickTag::empty) {
			syncBrick(index);
		}
	}
}

auto Volume::instanceOf(const Volume& source) -> Volume {
	assert(source.m_pool != nullptr);
	Volume out(*source.m_pool, source.m_brick_dims);

	out.m_texture_origin = source.m_texture_origin;
	out.m_entries = source.m_entries;
	out.m_brick_solid = source.m_brick_solid;
	out.m_solid_mask = source.m_solid_mask;
	out.m_solid_voxels = source.m_solid_voxels;
	for (BrickEntry& entry : out.m_entries) {
		if (entry.isPooled()) {
			source.m_pool->retain(entry.payload());
			entry = BrickEntry::make(BrickTag::shared, entry.payload());
		}
	}
	return out;
}

void Volume::reshareFrom(const Volume& source) {
	assert(source.m_pool != nullptr);
	if (m_pool != source.m_pool || this == &source) {
		*this = instanceOf(source);
		return;
	}

	const auto same_brick = [](BrickEntry a, BrickEntry b) { return a.isPooled() && b.isPooled() && a.payload() == b.payload(); };
	const size_t old_count = m_entries.size();
	const size_t new_count = source.m_entries.size();
	// retain everything new first so a brick that only moved index never drops to zero in between
	for (size_t index = 0; index < new_count; ++index) {
		const BrickEntry entry = source.m_entries[index];
		if (entry.isPooled() && not(index < old_count && same_brick(m_entries[index], entry))) {
			m_pool->retain(entry.payload());
		}
	}
	for (size_t index = 0; index < old_count; ++index) {
		const BrickEntry entry = m_entries[index];
		if (entry.isPooled() && not(index < new_count && same_brick(entry, source.m_entries[index]))) {
			m_pool->free(entry.payload());
		}
	}

	m_brick_dims = source.m_brick_dims;
	m_texture_origin = source.m_texture_origin;
	m_entries = source.m_entries;
	m_brick_solid = source.m_brick_solid;
	m_solid_mask = source.m_solid_mask;
	m_solid_voxels = source.m_solid_voxels;
	for (BrickEntry& entry : m_entries) {
		if (entry.isPooled()) {
			entry = BrickEntry::make(BrickTag::shared, entry.payload());
		}
	}
	m_revision = 0;
	m_id = nextVolumeId();
	m_dirty_mask.clear();
	m_dirty_list.clear();
	m_dirty_since = 0;
}

auto Volume::adoptResized(Volume& source, glm::ivec3 brick_offset, glm::uvec3 brick_dims) -> Volume {
	assert(source.m_pool != nullptr);
	Volume out(*source.m_pool, brick_dims);

	out.m_texture_origin = source.m_texture_origin - (brick_offset * static_cast<int32_t>(k_brick_dim));

	for (uint32_t index = 0; index < source.m_entries.size(); ++index) {
		BrickEntry& entry = source.m_entries[index];
		if (entry.tag() == BrickTag::empty) {
			continue;
		}

		const glm::ivec3 target = source.brickAtIndex(index) + brick_offset;
		if (!out.containsBrick(target)) {
			continue;
		}

		out.m_entries[out.entryIndex(target)] = entry;
		if (entry.isPooled()) {
			source.m_pool->retain(entry.payload());
		}
		if (entry.tag() == BrickTag::owned) {
			entry = BrickEntry::make(BrickTag::shared, entry.payload());
		}
	}
	out.recountAll();
	return out;
}

void Volume::releaseOwned() {
	if (m_pool == nullptr) {
		return;
	}
	for (BrickEntry& entry : m_entries) {
		if (entry.isPooled()) {
			m_pool->free(entry.payload());
			entry = BrickEntry {};
		}
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
	if (entry.tag() == BrickTag::owned && m_pool->referenceCount(entry.payload()) == 1) {
		return entry.payload();
	}

	// uniform and pooled sources overwrite every byte below, only an empty one needs the clear
	const uint32_t id = m_pool->allocate(entry.tag() == BrickTag::empty);
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
		case BrickTag::owned:
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

	if (entry.isPooled()) {
		m_pool->free(entry.payload());
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
	syncBrick(index);

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
	if (m_entries[index].isPooled()) {
		m_pool->free(m_entries[index].payload());
	}

	m_entries[index] = next;
	syncBrick(index);
	++m_revision;
	markDirty(brick);
}

auto Volume::tryCollapseUniform(glm::ivec3 brick) -> bool {
	if (!containsBrick(brick)) {
		return false;
	}

	const uint32_t index = entryIndex(brick);
	const BrickEntry entry = m_entries[index];
	if (!entry.isPooled()) {
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
	syncBrick(index);
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

	const uint32_t index = entryIndex(brick);
	const uint32_t id = makeWritable(index);
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
	syncBrick(index);
	++m_revision;
	markDirty(brick);
	return true;
}

auto Volume::takeBrick(glm::ivec3 brick) -> BrickEntry {
	if (!containsBrick(brick)) {
		return BrickEntry {};
	}

	const uint32_t index = entryIndex(brick);
	const BrickEntry entry = m_entries[index];
	if (entry.tag() == BrickTag::empty) {
		return entry;
	}

	m_entries[index] = BrickEntry {};
	syncBrick(index);
	++m_revision;
	markDirty(brick);
	return entry;
}

void Volume::adoptBrick(glm::ivec3 brick, BrickEntry entry) {
	if (!containsBrick(brick)) {
		return;
	}

	const uint32_t index = entryIndex(brick);
	assert(m_entries[index].tag() == BrickTag::empty);
	if (m_entries[index].isPooled()) {
		m_pool->free(m_entries[index].payload());
	}

	m_entries[index] = entry;
	syncBrick(index);
	++m_revision;
	markDirty(brick);
}

auto Volume::clearVoxels(glm::ivec3 brick, const BrickOccupancy& mask) -> bool {
	const BrickOccupancy* occupancy = occupancyPointer(brick);
	if (occupancy == nullptr) {
		return true;
	}

	const BrickOccupancy affected = mask & *occupancy;
	if (isEmpty(affected)) {
		return true;
	}

	const uint32_t index = entryIndex(brick);
	if (affected == *occupancy) {
		if (m_entries[index].isPooled()) {
			m_pool->free(m_entries[index].payload());
		}
		m_entries[index] = BrickEntry {};
		syncBrick(index);
		++m_revision;
		markDirty(brick);
		return true;
	}

	const uint32_t id = makeWritable(index);
	if (id == k_invalid_brick) {
		return false;
	}

	std::span<uint8_t, k_brick_material_bytes> bytes = m_pool->material(id);
	BrickOccupancy& stored = m_pool->occupancy(id);
	forEachSetVoxel(affected, [&](uint32_t local_index, uint32_t, uint32_t, uint32_t) {
		bytes[local_index] = k_empty_palette_index;
	});
	stored = stored & ~affected;
	syncBrick(index);

	++m_revision;
	markDirty(brick);
	return true;
}

auto Volume::copyBrickFrom(glm::ivec3 brick, const Volume& source, glm::ivec3 source_brick) -> bool {
	if (!containsBrick(brick)) {
		return false;
	}
	const BrickEntry from = source.entryAt(source_brick);
	if (from.tag() == BrickTag::empty || from.tag() == BrickTag::uniform) {
		const uint32_t before = m_revision;
		setBrickUniform(brick, from.tag() == BrickTag::empty ? k_empty_palette_index : static_cast<uint8_t>(from.payload()));
		return m_revision != before;
	}

	const uint32_t index = entryIndex(brick);
	const std::span<const uint8_t, k_brick_material_bytes> bytes = std::as_const(*source.m_pool).material(from.payload());
	const BrickEntry current = m_entries[index];
	if (current.isPooled() && std::ranges::equal(bytes, std::as_const(*m_pool).material(current.payload()))) {
		return false;
	}

	const uint32_t id = makeWritable(index);
	if (id == k_invalid_brick) {
		return false;
	}
	std::ranges::copy(bytes, m_pool->material(id).begin());
	m_pool->occupancy(id) = std::as_const(*source.m_pool).occupancy(from.payload());
	syncBrick(index);
	++m_revision;
	markDirty(brick);
	return true;
}

auto Volume::beginBrickWrite(glm::ivec3 brick) -> std::optional<WritableBrick> {
	if (!containsBrick(brick)) {
		return std::nullopt;
	}
	const uint32_t id = makeWritable(entryIndex(brick));
	if (id == k_invalid_brick) {
		return std::nullopt;
	}
	return WritableBrick {.material = m_pool->material(id), .occupancy = &m_pool->occupancy(id)};
}

void Volume::finishBrickWrite(glm::ivec3 brick, bool changed) {
	if (!containsBrick(brick)) {
		return;
	}
	const uint32_t index = entryIndex(brick);
	const BrickEntry entry = m_entries[index];
	if (entry.tag() != BrickTag::owned) {
		return;
	}
	if (isEmpty(m_pool->occupancy(entry.payload()))) {
		m_pool->free(entry.payload());
		m_entries[index] = BrickEntry {};
	} else if (changed) {
		tryCollapseUniform(brick);
	}
	// the caller wrote the occupancy itself so recount whether or not it says it changed
	syncBrick(index);
	if (changed) {
		++m_revision;
		markDirty(brick);
	}
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
	return {
	  static_cast<int>(index % m_brick_dims.x),
	  static_cast<int>((index / m_brick_dims.x) % m_brick_dims.y),
	  static_cast<int>(index / layer)
	};
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
