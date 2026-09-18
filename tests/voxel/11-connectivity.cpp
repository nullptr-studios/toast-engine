#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <toast/voxel/connectivity.hpp>
#include <vector>

using namespace voxel;
using namespace voxeltest;

namespace {

struct Labels {
	std::vector<uint32_t> of;
	uint32_t count = 0;
};

[[nodiscard]]
auto flatIndex(glm::ivec3 dims, glm::ivec3 v) -> size_t {
	return static_cast<size_t>(v.x + v.y * dims.x + v.z * dims.x * dims.y);
}

template<typename Solid>
[[nodiscard]]
auto floodFill(glm::ivec3 dims, Solid&& solid) -> Labels {
	Labels out;
	out.of.assign(static_cast<size_t>(dims.x * dims.y * dims.z), k_no_component);
	forEachCell(dims, [&](glm::ivec3 start) {
		if (!solid(start) || out.of[flatIndex(dims, start)] != k_no_component) {
			return;
		}
		std::vector<glm::ivec3> open {start};
		out.of[flatIndex(dims, start)] = out.count;
		while (!open.empty()) {
			const glm::ivec3 current = open.back();
			open.pop_back();
			for (const glm::ivec3& step : k_steps) {
				const glm::ivec3 next = current + step;
				const bool inside = glm::all(glm::greaterThanEqual(next, glm::ivec3(0))) && glm::all(glm::lessThan(next, dims));
				if (inside && solid(next) && out.of[flatIndex(dims, next)] == k_no_component) {
					out.of[flatIndex(dims, next)] = out.count;
					open.push_back(next);
				}
			}
		}
		++out.count;
	});
	return out;
}

/// Equal up to renaming components
template<typename Label>
[[nodiscard]]
auto samePartition(glm::ivec3 dims, const Labels& expected, Label&& label) -> bool {
	std::map<uint32_t, uint32_t> forward;
	std::map<uint32_t, uint32_t> backward;
	bool same = true;
	forEachCell(dims, [&](glm::ivec3 v) {
		const uint32_t mine = label(v);
		const uint32_t theirs = expected.of[flatIndex(dims, v)];
		if (mine == k_no_component || theirs == k_no_component) {
			same = same && mine == theirs;
			return;
		}
		same = same && forward.emplace(mine, theirs).first->second == theirs && backward.emplace(theirs, mine).first->second == mine;
	});
	return same && forward.size() == expected.count;
}

}

TOAST_TEST_NAMED("voxel", "voxel/11-connectivity", test_voxel_11_connectivity) {
	{
		assert(brickComponents(k_empty_brick).empty());
		assert(brickComponents(k_full_brick).size() == 1);

		BrickOccupancy edge_and_corner {};
		setSolid(edge_and_corner, 0, 0, 0, true);
		setSolid(edge_and_corner, 1, 1, 0, true);
		setSolid(edge_and_corner, 2, 2, 1, true);
		assert(brickComponents(edge_and_corner).size() == 3);

		BrickOccupancy ordered {};
		setSolid(ordered, 5, 5, 5, true);
		setSolid(ordered, 0, 0, 1, true);
		const std::vector<BrickOccupancy> parts = brickComponents(ordered);
		assert(parts.size() == 2 && isSolid(parts[0], 0, 0, 1));
	}

	Rng rng {0xC0DE'0007'C0DE'0007ull};
	for (uint32_t i = 0; i < 96; ++i) {
		const BrickOccupancy brick = randomBrick(rng, 10 + i % 7 * 10);
		const Labels expected = floodFill(glm::ivec3(8), [&brick](glm::ivec3 v) { return isSolidAt(brick, v); });
		const std::vector<BrickOccupancy> parts = brickComponents(brick);

		uint32_t covered = 0;
		for (const BrickOccupancy& part : parts) {
			covered += popCount(part);
		}
		const auto partOf = [&parts](glm::ivec3 v) {
			for (uint32_t p = 0; p < parts.size(); ++p) {
				if (isSolidAt(parts[p], v)) {
					return p;
				}
			}
			return k_no_component;
		};
		assert(parts.size() == expected.count && covered == popCount(brick));
		assert(samePartition(glm::ivec3(8), expected, partOf));
	}

	{
		BrickPool pool(8);
		Volume volume(pool, glm::uvec3(3, 1, 1));
		assert(analyseConnectivity(volume).component_count == 0);

		forEachCell(glm::ivec3(3, 1, 1), [&volume](glm::ivec3 brick) { volume.setBrickUniform(brick, 5); });
		const Connectivity whole = analyseConnectivity(volume);
		assert(whole.component_count == 1 && whole.pieces.size() == 3);

		forEachCell(glm::ivec3(1, 8, 8), [&volume](glm::ivec3 v) { volume.setVoxel(v + glm::ivec3(12, 0, 0), k_empty_palette_index); });
		const Connectivity split = analyseConnectivity(volume);
		assert(split.component_count == 2 && split.pieces.size() == 4);
		assert(split.componentOf(glm::ivec3(0)) != split.componentOf(glm::ivec3(23, 7, 7)));
		assert(split.componentOf(glm::ivec3(12, 3, 3)) == k_no_component);
	}

	for (uint32_t i = 0; i < 48; ++i) {
		BrickPool pool(64);
		Volume volume(pool, glm::uvec3(3, 2, 2));
		forEachCell(glm::ivec3(3, 2, 2), [&](glm::ivec3 brick) { fillRandom(volume, brick, rng, 10 + i % 7 * 10); });

		const glm::ivec3 dims(volume.voxelDims());
		const Labels expected = floodFill(dims, [&volume](glm::ivec3 v) { return volume.isSolidAt(v); });
		const Connectivity result = analyseConnectivity(volume);
		assert(result.component_count == expected.count);
		assert(samePartition(dims, expected, [&result](glm::ivec3 v) { return result.componentOf(v); }));

		uint32_t next = 0;
		for (const BrickPiece& piece : result.pieces) {
			assert(piece.component <= next);
			next = std::max(next, piece.component + 1);
		}
	}
}
