#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <array>
#include <cassert>
#include <glm/gtc/matrix_transform.hpp>
#include <set>
#include <toast/voxel/stamp.hpp>

using namespace voxel;
using namespace voxeltest;

TOAST_TEST_NAMED("voxel", "voxel/15-stamp", test_voxel_15_stamp) {
	const std::array<LatticeOrientation, 48> all = allLatticeOrientations();
	const PaletteRemapTable identity = identityRemap();
	assert(all[0] == LatticeOrientation {});

	{
		std::set<std::array<int, 6>> distinct;
		int mirrors = 0;
		for (const LatticeOrientation& o : all) {
			distinct.insert(std::array<int, 6> {o.source[0], o.source[1], o.source[2], o.flip[0], o.flip[1], o.flip[2]});
			mirrors += o.isMirror() ? 1 : 0;
			assert(glm::determinant(toMatrix(o)) == (o.isMirror() ? -1.0f : 1.0f));

			const LatticePlacement placement {o, glm::ivec3(5, -3, 11)};
			for (const glm::ivec3& v : {glm::ivec3(0), glm::ivec3(1, 2, 3), glm::ivec3(-2, 4, -6), glm::ivec3(15, 9, 1)}) {
				assert(placeVoxel(placement, v) == glm::ivec3(glm::floor(toMatrix(o) * (glm::vec3(v) + 0.5f))) + placement.offset);
			}

			for (const glm::ivec3& offset : {glm::ivec3(0), glm::ivec3(12, -7, 300), glm::ivec3(10000, -10000, 5000)}) {
				glm::mat4 transform(toMatrix(o));
				transform[3] = glm::vec4(glm::vec3(offset) * k_voxel_size, 1.0f);
				assert((placementFromTransform(transform) == LatticePlacement {o, offset}));

				transform[0][0] += 1e-6f;
				transform[3][2] += 1e-5f;
				assert((placementFromTransform(transform) == LatticePlacement {o, offset}));
			}
		}
		assert(distinct.size() == 48 && mirrors == 24);
	}

	{
		LatticeOrientation quarter;
		quarter.source = {1, 0, 2};
		quarter.flip = {true, false, false};
		const glm::mat4 identity_matrix(1.0f);
		const glm::vec3 z_axis(0.0f, 0.0f, 1.0f);
		assert(placementFromTransform(glm::rotate(identity_matrix, glm::radians(90.0f), z_axis))->orientation == quarter);

		glm::mat4 shear(1.0f);
		shear[1][0] = 0.3f;
		glm::mat4 projective(1.0f);
		projective[0][3] = 0.2f;
		for (const glm::mat4& rejected :
		     {glm::translate(identity_matrix, glm::vec3(0.05f, 0.0f, 0.0f)), glm::translate(identity_matrix, glm::vec3(0.0f, 0.002f, 0.0f)),
		      glm::rotate(identity_matrix, glm::radians(45.0f), z_axis), glm::rotate(identity_matrix, glm::radians(90.5f), z_axis),
		      glm::scale(identity_matrix, glm::vec3(1.0f, 1.0f, 2.0f)), shear, projective}) {
			assert(!placementFromTransform(rejected).has_value());
		}
	}

	BrickPool piece_pool(8);
	Volume piece(piece_pool, glm::uvec3(2, 1, 1));
	Rng rng {0x57A4'9000'0000'0010ull};
	fillRandom(piece, glm::ivec3(0), rng, 45);
	fillRandom(piece, glm::ivec3(1, 0, 0), rng, 45);

	for (const LatticeOrientation& o : all) {
		BrickPool pool(256);
		Volume world(pool, glm::uvec3(5, 5, 5));
		const LatticePlacement placement {o, glm::ivec3(21, 19, 22)};

		const StampResult result = stamp(world, piece, placement, identity);
		assert(!result.pool_exhausted && result.voxels_clipped == 0 && result.voxels_unmapped == 0);
		assert(result.voxels_written == piece.solidVoxelCount() && world.solidVoxelCount() == piece.solidVoxelCount());
		forEachCell(glm::ivec3(16, 8, 8), [&](glm::ivec3 v) {
			assert(!piece.isSolidAt(v) || world.materialAt(placeVoxel(placement, v)) == piece.materialAt(v));
		});
	}

	{
		BrickPool source_pool(4);
		Volume block(source_pool, glm::uvec3(1, 1, 1));
		forEachCell(glm::ivec3(8), [&block](glm::ivec3 v) { block.setVoxel(v, 5); });

		BrickPool pool(16);
		Volume world(pool, glm::uvec3(3, 3, 3));
		LatticeOrientation flipped;
		flipped.flip = {true, false, false};
		assert(stamp(world, block, LatticePlacement {LatticeOrientation {}, glm::ivec3(8, 16, 8)}, identity).voxels_written == 512);
		assert(stamp(world, block, LatticePlacement {flipped, glm::ivec3(16, 0, 0)}, identity).voxels_written == 512);
		assert(world.entryAt(glm::ivec3(1, 2, 1)).tag() == BrickTag::uniform && world.entryAt(glm::ivec3(1, 0, 0)).tag() == BrickTag::uniform);
		assert(pool.allocatedCount() == 0);

		assert(stamp(world, block, LatticePlacement {LatticeOrientation {}, glm::ivec3(3, 16, 16)}, identity).voxels_written == 512);
		assert(world.materialAt(glm::ivec3(3, 16, 16)) == 5 && world.materialAt(glm::ivec3(10, 23, 23)) == 5);
		assert(world.materialAt(glm::ivec3(2, 16, 16)) == 0 && world.materialAt(glm::ivec3(11, 16, 16)) == 0);
		assert(world.entryAt(glm::ivec3(0, 2, 2)).tag() == BrickTag::owned && world.solidVoxelCount() == 512 * 3);
	}

	{
		PaletteEntry red;
		red.albedo_r = 200;
		red.material = 1;
		PaletteEntry glow;
		glow.albedo_b = 200;
		glow.emissive = 100;

		Palette master;
		master.max_emissive = 4.0f;
		master.entries[40] = red;
		master.entries[41] = glow;

		Palette painted_palette;
		painted_palette.max_emissive = 2.0f;
		painted_palette.entries[1] = red;
		painted_palette.entries[2] = glow;
		const PaletteRemap remap = buildRemap(painted_palette, master);
		assert(remap.table[0] == 0 && remap.table[1] == 40 && remap.table[2] == 0 && remap.table[3] == 0);
		assert(remap.unmatched == std::vector<uint8_t> {2});
		painted_palette.max_emissive = 4.0f;
		assert(buildRemap(painted_palette, master).table[2] == 41);

		BrickPool source_pool(4);
		Volume painted(source_pool, glm::uvec3(1, 1, 1));
		painted.setVoxel(glm::ivec3(0), 1);
		painted.setVoxel(glm::ivec3(1, 0, 0), 2);
		painted.setVoxel(glm::ivec3(2, 0, 0), 2);

		BrickPool pool(4);
		Volume world(pool, glm::uvec3(1, 1, 1));
		world.setVoxel(glm::ivec3(2, 0, 0), 7);
		const StampResult result = stamp(world, painted, LatticePlacement {}, remap.table);
		assert(result.voxels_written == 1 && result.voxels_unmapped == 2);
		assert(world.materialAt(glm::ivec3(0)) == 40 && world.materialAt(glm::ivec3(1, 0, 0)) == 0 && world.materialAt(glm::ivec3(2, 0, 0)) == 7);
	}

	{
		uint32_t inside = 0;
		forEachCell(glm::ivec3(8), [&](glm::ivec3 v) { inside += piece.isSolidAt(v) ? 1 : 0; });

		BrickPool pool(4);
		Volume world(pool, glm::uvec3(1, 1, 1));
		const StampResult result = stamp(world, piece, LatticePlacement {}, identity);
		assert(result.voxels_written == inside && result.voxels_clipped == piece.solidVoxelCount() - inside);

		BrickPool starved(1);
		Volume large(starved, glm::uvec3(3, 3, 3));
		assert(stamp(large, piece, LatticePlacement {LatticeOrientation {}, glm::ivec3(3)}, identity).pool_exhausted);
	}
}
