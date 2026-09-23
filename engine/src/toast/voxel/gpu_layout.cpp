#include "gpu_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <tracy/Tracy.hpp>

namespace voxel::gpu {

namespace {

[[nodiscard]]
auto poolSlotsInUse(const BrickPool& pool) -> uint32_t {
	uint32_t used = 0;
	while (used < pool.capacity() && pool.isValid(used)) {
		++used;
	}
	return used;
}

[[nodiscard]]
auto brickOfIndex(glm::uvec3 dims, uint32_t index) -> glm::uvec3 {
	return {index % dims.x, (index / dims.x) % dims.y, index / (dims.x * dims.y)};
}

void bump(TagDelta& delta, BrickTag tag, int32_t by) {
	switch (tag) {
		case BrickTag::uniform: delta.uniform += by; break;
		case BrickTag::shared: delta.shared += by; break;
		case BrickTag::owned: delta.owned += by; break;
		case BrickTag::empty: break;
	}
}

/// Sorted unique destination words become runs of consecutive words
void appendWords(PatchSection& section, std::span<const uint32_t> destinations, std::span<const uint32_t> source) {
	for (size_t first = 0; first < destinations.size();) {
		size_t end = first + 1;
		while (end < destinations.size() && destinations[end] == destinations[end - 1] + 1) {
			++end;
		}
		section.runs.push_back(
		    {.dst = destinations[first],
		     .count = static_cast<uint32_t>(end - first),
		     .src = static_cast<uint32_t>(section.words.size())}
		);
		for (size_t i = first; i < end; ++i) {
			section.words.push_back(source[destinations[i]]);
		}
		first = end;
	}
}

/// Sorted unique slots become runs of consecutive slots
template<typename Fill>
void appendSlots(PatchSection& section, std::span<const uint32_t> slots, uint32_t words_per_slot, Fill&& fill) {
	for (size_t first = 0; first < slots.size();) {
		size_t end = first + 1;
		while (end < slots.size() && slots[end] == slots[end - 1] + 1) {
			++end;
		}
		const size_t base = section.words.size();
		section.runs.push_back(
		    {.dst = slots[first] * words_per_slot,
		     .count = static_cast<uint32_t>(end - first) * words_per_slot,
		     .src = static_cast<uint32_t>(base)}
		);
		section.words.resize(base + ((end - first) * words_per_slot));
		for (size_t i = first; i < end; ++i) {
			fill(slots[i], section.words.data() + base + ((i - first) * words_per_slot));
		}
		first = end;
	}
}

void sortUnique(std::vector<uint32_t>& values) {
	std::ranges::sort(values);
	values.erase(std::ranges::unique(values).begin(), values.end());
}

}

auto packPool(const BrickPool& pool) -> PackedPool {
	ZoneScoped;

	const uint32_t used = poolSlotsInUse(pool);

	PackedPool out;
	out.materials.assign(static_cast<size_t>(used) * k_material_words_per_brick, 0u);
	out.occupancy.assign(static_cast<size_t>(used) * k_occupancy_words_per_brick, 0u);

	for (uint32_t id = 0; id < used; ++id) {
		// Little endian host so byte i is the low byte of word i / 4 >> 8 * (i % 4)
		const std::span<const uint8_t, k_brick_material_bytes> bytes = pool.material(id);
		std::memcpy(
		    out.materials.data() + (static_cast<size_t>(id) * k_material_words_per_brick), bytes.data(), k_brick_material_bytes
		);

		const BrickOccupancy& occupancy = pool.occupancy(id);
		const size_t base = static_cast<size_t>(id) * k_occupancy_words_per_brick;
		for (uint32_t slice = 0; slice < k_brick_dim; ++slice) {
			out.occupancy[base + (slice * 2)] = static_cast<uint32_t>(occupancy.slices[slice]);
			out.occupancy[base + (slice * 2) + 1] = static_cast<uint32_t>(occupancy.slices[slice] >> 32u);
		}
	}
	return out;
}

auto packLayout(std::span<const SceneVolume> volumes) -> SceneLayout {
	ZoneScoped;

	SceneLayout out;
	out.records.reserve(volumes.size());
	std::vector<const Palette*> packed_palettes;

	for (const SceneVolume& entry : volumes) {
		assert(entry.volume != nullptr && entry.palette != nullptr && "a scene volume needs a volume and a palette");
		const glm::uvec3 dims = entry.volume->brickDims();
		const glm::uvec3 coarse = coarseDims(dims);

		VolumeRecord record;
		record.brick_dims_x = dims.x;
		record.brick_dims_y = dims.y;
		record.brick_dims_z = dims.z;
		record.grid_offset = out.grid_words;
		record.coarse_dims_x = coarse.x;
		record.coarse_dims_y = coarse.y;
		record.coarse_dims_z = coarse.z;
		record.coarse_offset = out.coarse_words;

		out.grid_words += dims.x * dims.y * dims.z;
		out.coarse_words += ((coarse.x * coarse.y * coarse.z) + 31u) / 32u;

		const auto found = std::find(packed_palettes.begin(), packed_palettes.end(), entry.palette);
		if (found != packed_palettes.end()) {
			record.palette_offset = static_cast<uint32_t>(found - packed_palettes.begin()) * k_palette_size;
		} else {
			record.palette_offset = static_cast<uint32_t>(packed_palettes.size()) * k_palette_size;
			const size_t base = out.palettes.size();
			out.palettes.resize(base + k_palette_words);
			std::memcpy(out.palettes.data() + base, entry.palette->entries.data(), sizeof(PaletteEntry) * k_palette_size);
			packed_palettes.push_back(entry.palette);
		}
		record.max_emissive = entry.palette->max_emissive;

		out.records.push_back(record);
	}
	return out;
}

auto packScene(std::span<const SceneVolume> volumes) -> PackedScene {
	ZoneScoped;

	SceneLayout layout = packLayout(volumes);

	PackedScene out;
	out.records = std::move(layout.records);
	out.palettes = std::move(layout.palettes);
	out.grids.assign(layout.grid_words, 0u);
	out.coarse.assign(layout.coarse_words, 0u);

	for (size_t i = 0; i < volumes.size(); ++i) {
		const Volume& volume = *volumes[i].volume;
		const VolumeRecord& record = out.records[i];
		const glm::uvec3 dims = volume.brickDims();
		const glm::uvec3 coarse = coarseDims(dims);

		for (uint32_t z = 0; z < dims.z; ++z) {
			for (uint32_t y = 0; y < dims.y; ++y) {
				for (uint32_t x = 0; x < dims.x; ++x) {
					const BrickEntry brick_entry = volume.entryAt(glm::ivec3(x, y, z));
					out.grids[record.grid_offset + gridIndex(dims, glm::uvec3(x, y, z))] = brick_entry.value;
					if (brick_entry.tag() == BrickTag::empty) {
						continue;
					}

					// Volume frees emptied bricks so any non empty tag means something solid
					const uint32_t cell = gridIndex(coarse, glm::uvec3(x, y, z) / k_coarse_bricks);
					out.coarse[record.coarse_offset + (cell / 32u)] |= 1u << (cell % 32u);
				}
			}
		}
	}
	return out;
}

auto layoutMatches(const PackedScene& retained, const SceneLayout& layout) -> bool {
	return retained.records.size() == layout.records.size() &&
	       std::memcmp(retained.records.data(), layout.records.data(), layout.records.size() * sizeof(VolumeRecord)) == 0 &&
	       retained.palettes == layout.palettes && retained.grids.size() == layout.grid_words &&
	       retained.coarse.size() == layout.coarse_words;
}

auto computePatch(
    PackedScene& retained, uint32_t retained_pool_slots, const BrickPool& pool, std::span<const SceneVolume> volumes,
    std::span<const VolumeDirty> dirty
) -> ScenePatch {
	ZoneScoped;

	ScenePatch patch;
	patch.pool_slots = std::max(poolSlotsInUse(pool), retained_pool_slots);
	patch.tags.assign(volumes.size(), TagDelta {});

	std::vector<uint32_t> slots;
	std::vector<uint32_t> grid_words;
	std::vector<uint32_t> coarse_words;
	std::vector<uint32_t> cells;

	for (const VolumeDirty& entry : dirty) {
		const Volume& volume = *volumes[entry.volume].volume;
		const VolumeRecord& record = retained.records[entry.volume];
		const glm::uvec3 dims(record.brick_dims_x, record.brick_dims_y, record.brick_dims_z);
		const glm::uvec3 coarse(record.coarse_dims_x, record.coarse_dims_y, record.coarse_dims_z);
		TagDelta& tags = patch.tags[entry.volume];

		cells.clear();
		for (const uint32_t index : entry.bricks) {
			const glm::uvec3 brick = brickOfIndex(dims, index);
			const BrickEntry now = volume.entryAt(glm::ivec3(brick));

			uint32_t& word = retained.grids[record.grid_offset + index];
			if (word != now.value) {
				bump(tags, BrickEntry {word}.tag(), -1);
				bump(tags, now.tag(), 1);
				word = now.value;
				grid_words.push_back(record.grid_offset + index);
			}

			// A brick written in place keeps its entry but not its bytes
			if (now.isPooled()) {
				slots.push_back(now.payload());
			}
			cells.push_back(gridIndex(coarse, brick / k_coarse_bricks));
		}

		sortUnique(cells);
		for (const uint32_t cell : cells) {
			const glm::uvec3 first = brickOfIndex(coarse, cell) * k_coarse_bricks;
			const glm::uvec3 last = glm::min(first + k_coarse_bricks, dims);

			bool occupied = false;
			for (uint32_t z = first.z; z < last.z && !occupied; ++z) {
				for (uint32_t y = first.y; y < last.y && !occupied; ++y) {
					for (uint32_t x = first.x; x < last.x && !occupied; ++x) {
						occupied = volume.entryAt(glm::ivec3(x, y, z)).tag() != BrickTag::empty;
					}
				}
			}

			const uint32_t word_index = record.coarse_offset + (cell / 32u);
			const uint32_t bit = 1u << (cell % 32u);
			if (((retained.coarse[word_index] & bit) != 0) != occupied) {
				retained.coarse[word_index] ^= bit;
				coarse_words.push_back(word_index);
			}
		}
	}

	sortUnique(grid_words);
	sortUnique(coarse_words);
	sortUnique(slots);
	appendWords(patch.grids, grid_words, retained.grids);
	appendWords(patch.coarse, coarse_words, retained.coarse);

	appendSlots(patch.materials, slots, k_material_words_per_brick, [&pool](uint32_t slot, uint32_t* out) {
		assert(pool.isValid(slot));
		std::memcpy(out, pool.material(slot).data(), k_brick_material_bytes);
	});
	appendSlots(patch.occupancy, slots, k_occupancy_words_per_brick, [&pool](uint32_t slot, uint32_t* out) {
		assert(pool.isValid(slot));
		const BrickOccupancy& occupancy = pool.occupancy(slot);
		for (uint32_t slice = 0; slice < k_brick_dim; ++slice) {
			out[slice * 2] = static_cast<uint32_t>(occupancy.slices[slice]);
			out[(slice * 2) + 1] = static_cast<uint32_t>(occupancy.slices[slice] >> 32u);
		}
	});
	return patch;
}

void applyPatch(PackedPool& pool, PackedScene& scene, const ScenePatch& patch) {
	pool.materials.resize(static_cast<size_t>(patch.pool_slots) * k_material_words_per_brick, 0u);
	pool.occupancy.resize(static_cast<size_t>(patch.pool_slots) * k_occupancy_words_per_brick, 0u);

	const auto apply = [](std::vector<uint32_t>& destination, const PatchSection& section) {
		for (const PatchRun& run : section.runs) {
			assert(static_cast<size_t>(run.dst) + run.count <= destination.size());
			std::copy_n(section.words.begin() + run.src, run.count, destination.begin() + run.dst);
		}
	};
	apply(pool.materials, patch.materials);
	apply(pool.occupancy, patch.occupancy);
	apply(scene.grids, patch.grids);
	apply(scene.coarse, patch.coarse);
}

auto patchWords(const ScenePatch& patch) -> size_t {
	return patch.materials.words.size() + patch.occupancy.words.size() + patch.grids.words.size() + patch.coarse.words.size();
}

auto sampleMaterial(const PackedPool& pool, const PackedScene& scene, uint32_t volume, glm::ivec3 voxel) -> uint8_t {
	const VolumeRecord& record = scene.records[volume];
	const glm::ivec3 dims(record.brick_dims_x, record.brick_dims_y, record.brick_dims_z);
	if (glm::any(glm::lessThan(voxel, glm::ivec3(0))) ||
	    glm::any(glm::greaterThanEqual(voxel, dims * static_cast<int32_t>(k_brick_dim)))) {
		return k_empty_palette_index;
	}

	const glm::uvec3 brick = glm::uvec3(voxel) >> 3u;
	const glm::uvec3 local = glm::uvec3(voxel) & 7u;
	const uint32_t value = scene.grids[record.grid_offset + gridIndex(glm::uvec3(dims), brick)];

	const uint32_t tag = value & 3u;
	const uint32_t payload = value >> k_brick_tag_bits;
	if (tag == static_cast<uint32_t>(BrickTag::empty)) {
		return k_empty_palette_index;
	}
	if (tag == static_cast<uint32_t>(BrickTag::uniform)) {
		return static_cast<uint8_t>(payload);
	}

	const uint32_t index = local.x + (local.y * k_brick_dim) + (local.z * k_brick_dim * k_brick_dim);
	const uint32_t word = pool.materials[(static_cast<size_t>(payload) * k_material_words_per_brick) + (index / 4u)];
	return static_cast<uint8_t>((word >> (8u * (index % 4u))) & 0xFFu);
}

auto sampleSolid(const PackedPool& pool, const PackedScene& scene, uint32_t volume, glm::ivec3 voxel) -> bool {
	const VolumeRecord& record = scene.records[volume];
	const glm::ivec3 dims(record.brick_dims_x, record.brick_dims_y, record.brick_dims_z);
	if (glm::any(glm::lessThan(voxel, glm::ivec3(0))) ||
	    glm::any(glm::greaterThanEqual(voxel, dims * static_cast<int32_t>(k_brick_dim)))) {
		return false;
	}

	const glm::uvec3 brick = glm::uvec3(voxel) >> 3u;
	const glm::uvec3 local = glm::uvec3(voxel) & 7u;
	const uint32_t value = scene.grids[record.grid_offset + gridIndex(glm::uvec3(dims), brick)];

	const uint32_t tag = value & 3u;
	if (tag == static_cast<uint32_t>(BrickTag::empty)) {
		return false;
	}
	if (tag == static_cast<uint32_t>(BrickTag::uniform)) {
		return true;    // uniform is full
	}

	// The low word holds rows y < 4
	const uint32_t bit = (local.y * k_brick_dim) + local.x;
	const size_t word_index =
	    (static_cast<size_t>(value >> k_brick_tag_bits) * k_occupancy_words_per_brick) + (local.z * 2u) + (bit / 32u);
	return ((pool.occupancy[word_index] >> (bit % 32u)) & 1u) != 0;
}

auto sampleCoarse(const PackedScene& scene, uint32_t volume, glm::ivec3 cell) -> bool {
	const VolumeRecord& record = scene.records[volume];
	const glm::ivec3 dims(record.coarse_dims_x, record.coarse_dims_y, record.coarse_dims_z);
	if (glm::any(glm::lessThan(cell, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(cell, dims))) {
		return false;
	}

	const uint32_t index = gridIndex(glm::uvec3(dims), glm::uvec3(cell));
	return ((scene.coarse[record.coarse_offset + (index / 32u)] >> (index % 32u)) & 1u) != 0;
}

auto samplePaletteEntry(const PackedScene& scene, uint32_t volume, uint8_t index) -> PaletteEntry {
	PaletteEntry out;
	const size_t base = (static_cast<size_t>(scene.records[volume].palette_offset) + index) * 4u;
	std::memcpy(&out, scene.palettes.data() + base, sizeof(PaletteEntry));
	return out;
}

}
