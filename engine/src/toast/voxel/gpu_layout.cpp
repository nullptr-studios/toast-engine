#include "gpu_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <tracy/Tracy.hpp>

namespace voxel::gpu {

auto packPool(const BrickPool& pool) -> PackedPool {
	ZoneScoped;

	uint32_t used = 0;
	while (used < pool.capacity() && pool.isValid(used)) {
		++used;
	}

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

auto packScene(std::span<const SceneVolume> volumes) -> PackedScene {
	ZoneScoped;

	PackedScene out;
	out.records.reserve(volumes.size());
	std::vector<const Palette*> packed_palettes;

	for (const SceneVolume& entry : volumes) {
		assert(entry.volume != nullptr && entry.palette != nullptr && "a scene volume needs a volume and a palette");
		const Volume& volume = *entry.volume;
		const glm::uvec3 dims = volume.brickDims();
		const glm::uvec3 coarse = coarseDims(dims);

		VolumeRecord record;
		record.brick_dims_x = dims.x;
		record.brick_dims_y = dims.y;
		record.brick_dims_z = dims.z;
		record.grid_offset = static_cast<uint32_t>(out.grids.size());
		record.coarse_dims_x = coarse.x;
		record.coarse_dims_y = coarse.y;
		record.coarse_dims_z = coarse.z;
		record.coarse_offset = static_cast<uint32_t>(out.coarse.size());

		const uint32_t coarse_cells = coarse.x * coarse.y * coarse.z;
		out.coarse.resize(out.coarse.size() + ((coarse_cells + 31u) / 32u), 0u);
		out.grids.reserve(out.grids.size() + (static_cast<size_t>(dims.x) * dims.y * dims.z));

		for (uint32_t z = 0; z < dims.z; ++z) {
			for (uint32_t y = 0; y < dims.y; ++y) {
				for (uint32_t x = 0; x < dims.x; ++x) {
					const BrickEntry brick_entry = volume.entryAt(glm::ivec3(x, y, z));
					out.grids.push_back(brick_entry.value);
					if (brick_entry.tag() == BrickTag::empty) {
						continue;
					}

					// Volume frees emptied bricks so any non empty tag means something solid
					const uint32_t cell = gridIndex(coarse, glm::uvec3(x, y, z) / k_coarse_bricks);
					out.coarse[record.coarse_offset + (cell / 32u)] |= 1u << (cell % 32u);
				}
			}
		}

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
