#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <array>
#include <cassert>

using namespace voxel;
using namespace voxeltest;

namespace {

/// Bit v * 8 + u where u and v are the two other axes in ascending order
[[nodiscard]]
auto referenceFace(const BrickOccupancy& brick, uint32_t face) -> uint64_t {
	const uint32_t axis = face / 2;
	const uint32_t u_axis = axis == 0 ? 1 : 0;
	const uint32_t v_axis = axis == 2 ? 1 : 2;
	uint64_t out = 0;
	for (int32_t v = 0; v < 8; ++v) {
		for (int32_t u = 0; u < 8; ++u) {
			glm::ivec3 voxel(0);
			voxel[axis] = face % 2 == 0 ? 0 : 7;
			voxel[u_axis] = u;
			voxel[v_axis] = v;
			if (isSolidAt(brick, voxel)) {
				out |= 1ull << (v * 8 + u);
			}
		}
	}
	return out;
}

}

TOAST_TEST_NAMED("voxel", "voxel/03-brick-faces", test_voxel_03_brick_faces) {
	Rng rng {0x5EED'1234'5EED'1234ull};
	uint32_t connected = 0;

	for (uint32_t i = 0; i < 128; ++i) {
		const BrickOccupancy a = randomBrick(rng, 1 + i % 20);
		const BrickOccupancy b = randomBrick(rng, 1 + i % 20);

		const BrickFaces faces = computeFaces(a);
		const std::array<uint64_t, 6> by_face {faces.neg_x, faces.pos_x, faces.neg_y, faces.pos_y, faces.neg_z, faces.pos_z};
		for (uint32_t face = 0; face < 6; ++face) {
			assert(by_face[face] == referenceFace(a, face));
		}

		bool contact = false;
		forEachCell(glm::ivec3(1, 8, 8), [&](glm::ivec3 v) {
			contact = contact || (isSolidAt(a, glm::ivec3(7, v.y, v.z)) && isSolidAt(b, glm::ivec3(0, v.y, v.z)));
		});
		assert(facesConnect(faces.pos_x, computeFaces(b).neg_x) == contact);
		connected += contact ? 1 : 0;
	}
	assert(connected > 0 && connected < 128);
}
