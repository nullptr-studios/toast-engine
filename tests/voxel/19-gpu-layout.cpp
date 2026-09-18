#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <toast/voxel/gpu_layout.hpp>

using namespace toast::voxel;
using namespace voxeltest;

TOAST_TEST_NAMED("voxel", "voxel/19-gpu-layout", test_voxel_19_gpu_layout) {
	Rng rng {0x1900'0000'0000'0019ull};
	BrickPool pool(64);

	Volume a(pool, glm::uvec3(3, 2, 2));
	fillRandom(a, glm::ivec3(0), rng, 40);
	fillRandom(a, glm::ivec3(1, 0, 0), rng, 40);
	fillRandom(a, glm::ivec3(2, 1, 1), rng, 15);
	a.setBrickUniform(glm::ivec3(1, 1, 0), 7);

	Volume b = Volume::instanceOf(a);
	b.setVoxel(glm::ivec3(3), 99);
	b.setVoxel(glm::ivec3(20, 12, 12), 0);

	Volume c(pool, glm::uvec3(9, 1, 5));
	c.setVoxel(glm::ivec3(0), 3);
	c.setVoxel(glm::ivec3(70, 2, 38), 5);

	Palette first;
	first.max_emissive = 8.0f;
	first.entries[7] = PaletteEntry {11, 0, 0, 200, 0, 0, 60, 3, k_entry_transparent, 9};
	Palette second;
	second.max_emissive = 2.0f;
	second.entries[5].albedo_b = 77;

	const std::array<gpu::SceneVolume, 3> volumes {{{&a, &first}, {&b, &first}, {&c, &second}}};
	const std::array<const Volume*, 3> sources {&a, &b, &c};
	const gpu::PackedPool packed = gpu::packPool(pool);
	const gpu::PackedScene scene = gpu::packScene(volumes);

	uint32_t used = 0;
	while (used < pool.capacity() && pool.isValid(used)) {
		++used;
	}
	assert(packed.materials.size() == used * gpu::k_material_words_per_brick);
	assert(packed.occupancy.size() == used * gpu::k_occupancy_words_per_brick);

	assert(scene.records.size() == 3 && scene.grids.size() == 12u + 12u + 45u);
	assert(scene.records[1].grid_offset == 12 && scene.records[2].grid_offset == 24);
	assert(scene.records[1].coarse_offset == 1 && scene.records[2].coarse_offset == 2);
	assert(scene.records[2].coarse_dims_x == 3 && scene.records[2].coarse_dims_y == 1 && scene.records[2].coarse_dims_z == 2);
	assert(scene.palettes.size() == 2u * gpu::k_palette_words);
	assert(scene.records[1].palette_offset == 0 && scene.records[2].palette_offset == 256 && scene.records[2].max_emissive == 2.0f);

	for (uint32_t v = 0; v < sources.size(); ++v) {
		const Volume& source = *sources[v];
		const glm::ivec3 dims(source.voxelDims());
		forEachCell(dims + 2, [&](glm::ivec3 p) {
			const glm::ivec3 voxel = p - 1;
			assert(gpu::sampleMaterial(packed, scene, v, voxel) == source.materialAt(voxel));
			assert(gpu::sampleSolid(packed, scene, v, voxel) == source.isSolidAt(voxel));
		});

		const glm::ivec3 bricks(source.brickDims());
		forEachCell(glm::ivec3(gpu::coarseDims(source.brickDims())) + 1, [&](glm::ivec3 cell) {
			bool occupied = false;
			forEachCell(glm::ivec3(4), [&](glm::ivec3 offset) {
				const glm::ivec3 brick = cell * 4 + offset;
				occupied = occupied || (glm::all(glm::lessThan(brick, bricks)) && source.entryAt(brick).tag() != BrickTag::empty);
			});
			assert(gpu::sampleCoarse(scene, v, cell) == occupied);
		});

		for (uint32_t i = 0; i < k_palette_size; ++i) {
			assert(gpu::samplePaletteEntry(scene, v, static_cast<uint8_t>(i)) == (v == 2 ? second : first).entries[i]);
		}
	}
	assert(gpu::sampleMaterial(packed, scene, 1, glm::ivec3(3)) == 99 && gpu::sampleMaterial(packed, scene, 0, glm::ivec3(12, 12, 4)) == 7);

	const uint32_t slot = a.entryAt(glm::ivec3(0)).payload();
	const uint32_t material_word = packed.materials[slot * gpu::k_material_words_per_brick];
	const uint32_t low = packed.occupancy[slot * gpu::k_occupancy_words_per_brick];
	const uint32_t high = packed.occupancy[slot * gpu::k_occupancy_words_per_brick + 1];
	for (int32_t x = 0; x < 4; ++x) {
		assert(((material_word >> (8 * x)) & 0xFFu) == a.materialAt(glm::ivec3(x, 0, 0)));
	}
	forEachCell(glm::ivec3(8, 8, 1), [&](glm::ivec3 v) {
		const int32_t bit = v.y * 8 + v.x;
		assert(((bit < 32 ? low >> bit : high >> (bit - 32)) & 1u) == (a.isSolidAt(v) ? 1u : 0u));
	});
}
