#include "connectivity.hpp"

#include <numeric>
#include <tracy/Tracy.hpp>
#include <utility>

namespace voxel {

auto analyseConnectivity(const Volume& volume) -> Connectivity {
	ZoneScoped;

	Connectivity out;
	const glm::uvec3 dims = volume.brickDims();
	const uint32_t brick_count = dims.x * dims.y * dims.z;
	const auto slot_of = [&](glm::ivec3 brick) {
		return static_cast<uint32_t>(brick.x) + (static_cast<uint32_t>(brick.y) * dims.x) +
		       (static_cast<uint32_t>(brick.z) * dims.x * dims.y);
	};

	// pieces of brick s are first_piece[s] .. first_piece[s + 1]
	std::vector<uint32_t> first_piece(brick_count + 1u, 0u);
	for (int32_t z = 0; std::cmp_less(z, dims.z); ++z) {
		for (int32_t y = 0; std::cmp_less(y, dims.y); ++y) {
			for (int32_t x = 0; std::cmp_less(x, dims.x); ++x) {
				const glm::ivec3 brick(x, y, z);
				first_piece[slot_of(brick)] = static_cast<uint32_t>(out.pieces.size());

				const BrickOccupancy* occupancy = volume.occupancyPointer(brick);
				if (occupancy == nullptr) {
					continue;
				}
				for (const BrickOccupancy& part : brickComponents(*occupancy)) {
					BrickPiece piece;
					piece.brick = brick;
					piece.voxels = part;
					out.pieces.push_back(piece);
				}
			}
		}
	}
	first_piece[brick_count] = static_cast<uint32_t>(out.pieces.size());

	// Lambdas since an anonymous namespace type could collide in the unity build
	const auto piece_count = static_cast<uint32_t>(out.pieces.size());
	std::vector<uint32_t> parent(piece_count);
	std::iota(parent.begin(), parent.end(), 0u);
	std::vector<uint32_t> set_size(piece_count, 1u);

	const auto find = [&](uint32_t i) {
		while (parent[i] != i) {
			parent[i] = parent[parent[i]];    // path halving
			i = parent[i];
		}
		return i;
	};
	const auto unite = [&](uint32_t a, uint32_t b) {
		a = find(a);
		b = find(b);
		if (a == b) {
			return;
		}
		if (set_size[a] < set_size[b]) {
			std::swap(a, b);
		}
		parent[b] = a;
		set_size[a] += set_size[b];
	};

	std::vector<BrickFaces> faces(piece_count);
	for (uint32_t i = 0; i < piece_count; ++i) {
		faces[i] = computeFaces(out.pieces[i].voxels);
	}

	for (int32_t z = 0; std::cmp_less(z, dims.z); ++z) {
		for (int32_t y = 0; std::cmp_less(y, dims.y); ++y) {
			for (int32_t x = 0; std::cmp_less(x, dims.x); ++x) {
				const glm::ivec3 brick(x, y, z);
				const uint32_t here = slot_of(brick);

				for (int32_t axis = 0; axis < 3; ++axis) {
					glm::ivec3 next = brick;
					next[axis] += 1;
					if (std::cmp_greater_equal(next[axis], dims[axis])) {
						continue;
					}
					const uint32_t there = slot_of(next);

					for (uint32_t p = first_piece[here]; p < first_piece[here + 1u]; ++p) {
						uint64_t outward = faces[p].pos_z;
						if (axis == 0) {
							outward = faces[p].pos_x;
						} else if (axis == 1) {
							outward = faces[p].pos_y;
						}
						for (uint32_t q = first_piece[there]; q < first_piece[there + 1u]; ++q) {
							uint64_t inward = faces[q].neg_z;
							if (axis == 0) {
								inward = faces[q].neg_x;
							} else if (axis == 1) {
								inward = faces[q].neg_y;
							}
							if (facesConnect(outward, inward)) {
								unite(p, q);
							}
						}
					}
				}
			}
		}
	}

	// Numbered by first appearance so labels do not depend on union find internals
	std::vector<uint32_t> label(piece_count, k_no_component);
	for (uint32_t i = 0; i < piece_count; ++i) {
		const uint32_t root = find(i);
		if (label[root] == k_no_component) {
			label[root] = out.component_count++;
		}
		out.pieces[i].component = label[root];
	}
	return out;
}

}
