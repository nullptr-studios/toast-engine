#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <cassert>
#include <cmath>
#include <toast/voxel/mass_accumulator.hpp>
#include <vector>

using namespace voxel;
using namespace voxeltest;

namespace {

struct Voxel {
	glm::ivec3 at {0};
	uint32_t density = 0;
};

[[nodiscard]]
auto close(double actual, double expected, double scale) -> bool {
	return std::abs(actual - expected) <= 1e-4 * (std::abs(scale) + 1e-12);
}

/// Point masses at voxel centres plus s squared over 6 on the diagonal for each cube
[[nodiscard]]
auto matchesReference(const MassProperties& actual, const std::vector<Voxel>& voxels) -> bool {
	const double s = k_voxel_size;
	double mass = 0.0;
	glm::dvec3 centre(0.0);
	for (const Voxel& v : voxels) {
		const double m = v.density * s * s * s;
		mass += m;
		centre += m * (glm::dvec3(v.at) + 0.5) * s;
	}
	centre /= mass;

	glm::dmat3 inertia(0.0);
	for (const Voxel& v : voxels) {
		const double m = v.density * s * s * s;
		const glm::dvec3 r = (glm::dvec3(v.at) + 0.5) * s - centre;
		inertia += m * (glm::dot(r, r) + s * s / 6.0) * glm::dmat3(1.0) - m * glm::outerProduct(r, r);
	}

	const double scale = inertia[0][0] + inertia[1][1] + inertia[2][2];
	bool same = close(actual.mass, mass, mass);
	for (int32_t a = 0; a < 3; ++a) {
		same = same && close(actual.center_of_mass[a], centre[a], 3.0);
		for (int32_t b = 0; b < 3; ++b) {
			same = same && close(actual.inertia[a][b], inertia[a][b], scale);
		}
	}
	return same;
}

}

TOAST_TEST_NAMED("voxel", "voxel/05-mass-moments", test_voxel_05_mass_moments) {
	assert(MassMoments {}.isEmpty());
	assert(resolve(MassMoments {}).mass == 0.0f);

	for (const glm::ivec3& size : {glm::ivec3(1), glm::ivec3(8, 3, 5)}) {
		MassMoments moments;
		forEachCell(size, [&moments](glm::ivec3 v) { moments.add(v.x, v.y, v.z, 2400); });
		const MassProperties p = resolve(moments);

		const glm::dvec3 extent = glm::dvec3(size) * static_cast<double>(k_voxel_size);
		const glm::dvec3 squared = extent * extent;
		const double mass = 2400.0 * extent.x * extent.y * extent.z;
		const double i_xx = mass * (squared.y + squared.z) / 12.0;
		const double i_zz = mass * (squared.x + squared.y) / 12.0;
		assert(close(p.mass, mass, mass));
		assert(close(p.center_of_mass.x, extent.x / 2.0, extent.x) && close(p.center_of_mass.z, extent.z / 2.0, extent.z));
		assert(close(p.inertia[0][0], i_xx, i_xx) && close(p.inertia[2][2], i_zz, i_zz));
	}

	Rng rng {0x11AA'22BB'33CC'44DDull};
	for (uint32_t i = 0; i < 32; ++i) {
		std::vector<Voxel> voxels(1 + rng.below(400));
		for (Voxel& v : voxels) {
			v = Voxel {glm::ivec3(rng.below(24), rng.below(17), rng.below(31)), 1 + rng.below(8000)};
		}

		MassMoments moments;
		MassMoments first_half;
		MassMoments second_half;
		MassMoments moved;
		for (size_t n = 0; n < voxels.size(); ++n) {
			const Voxel& v = voxels[n];
			moments.add(v.at.x, v.at.y, v.at.z, v.density);
			(n % 2 == 0 ? first_half : second_half).add(v.at.x, v.at.y, v.at.z, v.density);
			moved.add(v.at.x + 16, v.at.y - 40, v.at.z + 8, v.density);
		}
		assert(matchesReference(resolve(moments), voxels));
		assert((first_half += second_half) == moments);
		assert(moments.shifted(16, -40, 8) == moved);

		for (const Voxel& v : voxels) {
			moments.remove(v.at.x, v.at.y, v.at.z, v.density);
		}
		assert(moments == MassMoments {} && moments.isEmpty());
	}
}
