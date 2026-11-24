#include "connectivity.hpp"

namespace voxel {

auto analyseConnectivity(const Volume& volume, ConnectivityCache* cache) -> Connectivity {
	ZoneScopedN("voxel::AnalyseConnectivity");

	Connectivity out;
	const glm::uvec3 dims = volume.brickDims();
	const uint32_t brick_count = dims.x * dims.y * dims.z;
	const auto slot_of = [&](glm::ivec3 brick) {
		return static_cast<uint32_t>(brick.x) + (static_cast<uint32_t>(brick.y) * dims.x) +
		       (static_cast<uint32_t>(brick.z) * dims.x * dims.y);
	};

	// pieces of brick s are first_piece[s] .. first_piece[s + 1]
	std::vector<uint32_t> first_piece(brick_count + 1u, 0u);
	std::vector<BrickFaces> faces;
	const bool reuse = cache != nullptr && cache->dims == dims;
	size_t cursor = 0;
	uint32_t filled = 0;
	uint32_t reused = 0;
	volume.forEachSolidBrick([&](glm::ivec3 brick, uint32_t slot) {
		std::fill(first_piece.begin() + filled, first_piece.begin() + slot + 1, static_cast<uint32_t>(out.pieces.size()));
		filled = slot + 1;
		const BrickOccupancy& occupancy = *volume.occupancyPointer(brick);

		// Pieces of an unchanged brick come out the same and in the same order, so the last ones stand in for the flood fill
		if (reuse) {
			while (cursor < cache->pieces.size() && slot_of(cache->pieces[cursor].brick) < slot) {
				++cursor;
			}
			size_t end = cursor;
			BrickOccupancy merged {};
			while (end < cache->pieces.size() && slot_of(cache->pieces[end].brick) == slot) {
				merged = merged | cache->pieces[end].voxels;
				++end;
			}
			const bool unchanged = end > cursor && merged == occupancy;
			if (unchanged) {
				for (size_t i = cursor; i < end; ++i) {
					out.pieces.push_back(BrickPiece {.brick = brick, .voxels = cache->pieces[i].voxels});
					faces.push_back(cache->faces[i]);
				}
				++reused;
			}
			cursor = end;
			if (unchanged) {
				return;
			}
		}
		forEachBrickComponent(occupancy, [&](const BrickOccupancy& part) {
			out.pieces.push_back(BrickPiece {.brick = brick, .voxels = part});
			faces.push_back(computeFaces(part));
		});
	});
	std::fill(first_piece.begin() + filled, first_piece.end(), static_cast<uint32_t>(out.pieces.size()));
	ZoneValue(static_cast<uint64_t>(reused));

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

	volume.forEachSolidBrick([&](glm::ivec3 brick, uint32_t here) {
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
	});

	// Numbered by first appearance so labels do not depend on union find internals
	std::vector<uint32_t> label(piece_count, k_no_component);
	for (uint32_t i = 0; i < piece_count; ++i) {
		const uint32_t root = find(i);
		if (label[root] == k_no_component) {
			label[root] = out.component_count++;
		}
		out.pieces[i].component = label[root];
	}

	if (cache != nullptr) {
		cache->dims = dims;
		cache->pieces = out.pieces;
		cache->faces = std::move(faces);
	}
	return out;
}

}
