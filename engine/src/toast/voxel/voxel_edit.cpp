#include "voxel_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <tracy/Tracy.hpp>
#include <unordered_set>

namespace voxel {

namespace {

constexpr int32_t k_dim = static_cast<int32_t>(k_brick_dim);

[[nodiscard]]
auto brickOf(glm::ivec3 voxel) noexcept -> glm::ivec3 {
	return {voxel.x >> 3, voxel.y >> 3, voxel.z >> 3};
}

[[nodiscard]]
auto centreOf(glm::ivec3 voxel) noexcept -> glm::vec3 {
	return glm::vec3(voxel) + 0.5f;
}

[[nodiscard]]
auto floorToVoxel(glm::vec3 value) noexcept -> glm::ivec3 {
	return glm::ivec3(glm::floor(value));
}

[[nodiscard]]
auto clipToVolume(const Volume& volume, EditBounds bounds) noexcept -> EditBounds {
	bounds.min = glm::max(bounds.min, glm::ivec3(0));
	bounds.max = glm::min(bounds.max, glm::ivec3(volume.voxelDims()) - 1);
	return bounds;
}

auto writeVoxel(Volume& volume, glm::ivec3 voxel, uint8_t id, EditResult& result) -> bool {
	const Volume::VoxelWrite write = volume.setVoxel(voxel, id);
	if (write.changed) {
		++result.changed;
		return true;
	}
	if (write.previous_material != id) {
		result.pool_exhausted = true;
	}
	return false;
}

[[nodiscard]]
auto brickMayWrite(BrickEntry entry, const WriteBrush& brush) noexcept -> bool {
	switch (entry.tag()) {
		case BrickTag::empty:
			return brush.mode == WriteMode::replace ? brush.id != k_empty_palette_index
			                                        : canWrite(k_empty_palette_index, brush.mode, brush.match_id);
		case BrickTag::uniform: {
			const auto current = static_cast<uint8_t>(entry.payload());
			return current != brush.id && canWrite(current, brush.mode, brush.match_id);
		}
		default: return true;
	}
}

template<typename Inside>
auto fillShape(Volume& volume, EditBounds bounds, const WriteBrush& brush, Inside&& inside) -> EditResult {
	ZoneScoped;
	EditResult result;
	bounds = clipToVolume(volume, bounds);
	if (bounds.empty()) {
		return result;
	}

	const glm::ivec3 first_brick = brickOf(bounds.min);
	const glm::ivec3 last_brick = brickOf(bounds.max);
	for (int32_t bz = first_brick.z; bz <= last_brick.z; ++bz) {
		for (int32_t by = first_brick.y; by <= last_brick.y; ++by) {
			for (int32_t bx = first_brick.x; bx <= last_brick.x; ++bx) {
				const glm::ivec3 brick {bx, by, bz};
				if (!brickMayWrite(volume.entryAt(brick), brush)) {
					continue;
				}

				const glm::ivec3 lo = glm::max(bounds.min, brick * k_dim);
				const glm::ivec3 hi = glm::min(bounds.max, brick * k_dim + (k_dim - 1));
				bool touched = false;
				for (int32_t z = lo.z; z <= hi.z; ++z) {
					for (int32_t y = lo.y; y <= hi.y; ++y) {
						for (int32_t x = lo.x; x <= hi.x; ++x) {
							const glm::ivec3 voxel {x, y, z};
							if (!inside(centreOf(voxel)) || !canWrite(volume.materialAt(voxel), brush.mode, brush.match_id)) {
								continue;
							}
							touched = writeVoxel(volume, voxel, brush.id, result) || touched;
							if (result.pool_exhausted) {
								return result;
							}
						}
					}
				}
				if (touched && brush.id != k_empty_palette_index) {
					volume.tryCollapseUniform(brick);
				}
			}
		}
	}
	return result;
}

[[nodiscard]]
auto segmentParameter(glm::vec3 p, glm::vec3 a, glm::vec3 b) noexcept -> float {
	const glm::vec3 axis = b - a;
	const float length_squared = glm::dot(axis, axis);
	return length_squared > 0.0f ? glm::dot(p - a, axis) / length_squared : 0.0f;
}

}

auto boxBounds(glm::ivec3 a, glm::ivec3 b) noexcept -> EditBounds {
	return {glm::min(a, b), glm::max(a, b)};
}

auto sphereBounds(glm::vec3 center, float radius) noexcept -> EditBounds {
	const float r = std::max(radius, 0.0f);
	return {floorToVoxel(center - r), floorToVoxel(center + r)};
}

auto segmentBounds(glm::vec3 a, glm::vec3 b, float radius) noexcept -> EditBounds {
	const float r = std::max(radius, 0.0f);
	return {floorToVoxel(glm::min(a, b) - r), floorToVoxel(glm::max(a, b) + r)};
}

auto placedBounds(glm::uvec3 piece_brick_dims, const LatticePlacement& placement) noexcept -> EditBounds {
	const glm::ivec3 last = glm::ivec3(piece_brick_dims) * k_dim - 1;
	if (glm::any(glm::lessThan(last, glm::ivec3(0)))) {
		return {};
	}
	EditBounds out {glm::ivec3(std::numeric_limits<int32_t>::max()), glm::ivec3(std::numeric_limits<int32_t>::min())};
	for (int corner = 0; corner < 8; ++corner) {
		const glm::ivec3 local((corner & 1) != 0 ? last.x : 0, (corner & 2) != 0 ? last.y : 0, (corner & 4) != 0 ? last.z : 0);
		const glm::ivec3 landed = placeVoxel(placement, local);
		out.min = glm::min(out.min, landed);
		out.max = glm::max(out.max, landed);
	}
	return out;
}

auto boundsUnion(const EditBounds& a, const EditBounds& b) noexcept -> EditBounds {
	if (a.empty()) {
		return b;
	}
	if (b.empty()) {
		return a;
	}
	return {glm::min(a.min, b.min), glm::max(a.max, b.max)};
}

auto occupiedBounds(const Volume& volume) -> std::optional<EditBounds> {
	EditBounds out {glm::ivec3(std::numeric_limits<int32_t>::max()), glm::ivec3(std::numeric_limits<int32_t>::min())};
	bool any = false;
	for (uint32_t index = 0; index < volume.brickCount(); ++index) {
		const glm::ivec3 brick = volume.brickAtIndex(index);
		const BrickOccupancy* occupancy = volume.occupancyPointer(brick);
		if (occupancy == nullptr || isEmpty(*occupancy)) {
			continue;
		}
		const glm::ivec3 origin = brick * k_dim;
		if (isFull(*occupancy)) {
			out.min = glm::min(out.min, origin);
			out.max = glm::max(out.max, origin + (k_dim - 1));
			any = true;
			continue;
		}
		for (uint32_t i = 0; i < k_brick_voxel_count; ++i) {
			if (!isSolid(*occupancy, i)) {
				continue;
			}
			const BrickCoord local = localFromIndex(i);
			const glm::ivec3 voxel = origin + glm::ivec3(local.x, local.y, local.z);
			out.min = glm::min(out.min, voxel);
			out.max = glm::max(out.max, voxel);
			any = true;
		}
	}
	return any ? std::optional {out} : std::nullopt;
}

auto fillBox(Volume& volume, glm::ivec3 a, glm::ivec3 b, const WriteBrush& brush) -> EditResult {
	const EditBounds bounds = clipToVolume(volume, boxBounds(a, b));
	if (bounds.empty()) {
		return {};
	}

	EditResult result;
	if (brush.mode == WriteMode::replace) {
		const glm::ivec3 first_brick = brickOf(bounds.min);
		const glm::ivec3 last_brick = brickOf(bounds.max);
		std::vector<glm::ivec3> partial;
		for (int32_t bz = first_brick.z; bz <= last_brick.z; ++bz) {
			for (int32_t by = first_brick.y; by <= last_brick.y; ++by) {
				for (int32_t bx = first_brick.x; bx <= last_brick.x; ++bx) {
					const glm::ivec3 brick {bx, by, bz};
					const glm::ivec3 lo = brick * k_dim;
					const glm::ivec3 hi = lo + (k_dim - 1);
					if (glm::all(glm::greaterThanEqual(lo, bounds.min)) && glm::all(glm::lessThanEqual(hi, bounds.max))) {
						const BrickEntry before = volume.entryAt(brick);
						volume.setBrickUniform(brick, brush.id);
						if (volume.entryAt(brick) != before) {
							result.changed += k_brick_voxel_count;
						}
					} else {
						partial.push_back(brick);
					}
				}
			}
		}
		for (const glm::ivec3& brick : partial) {
			const EditBounds piece {glm::max(bounds.min, brick * k_dim), glm::min(bounds.max, brick * k_dim + (k_dim - 1))};
			result += fillShape(volume, piece, brush, [](glm::vec3) { return true; });
			if (result.pool_exhausted) {
				break;
			}
		}
		return result;
	}

	return fillShape(volume, bounds, brush, [](glm::vec3) { return true; });
}

auto fillSphere(Volume& volume, glm::vec3 center, float radius, const WriteBrush& brush) -> EditResult {
	const float radius_squared = radius * radius;
	return fillShape(volume, sphereBounds(center, radius), brush, [&](glm::vec3 p) {
		const glm::vec3 offset = p - center;
		return glm::dot(offset, offset) <= radius_squared;
	});
}

auto fillCylinder(Volume& volume, glm::vec3 a, glm::vec3 b, float radius, const WriteBrush& brush) -> EditResult {
	const float radius_squared = radius * radius;
	return fillShape(volume, segmentBounds(a, b, radius), brush, [&](glm::vec3 p) {
		const float t = segmentParameter(p, a, b);
		if (t < 0.0f || t > 1.0f) {
			return false;
		}
		const glm::vec3 offset = p - (a + ((b - a) * t));
		return glm::dot(offset, offset) <= radius_squared;
	});
}

auto fillCapsule(Volume& volume, glm::vec3 a, glm::vec3 b, float radius, const WriteBrush& brush) -> EditResult {
	const float radius_squared = radius * radius;
	return fillShape(volume, segmentBounds(a, b, radius), brush, [&](glm::vec3 p) {
		const float t = std::clamp(segmentParameter(p, a, b), 0.0f, 1.0f);
		const glm::vec3 offset = p - (a + ((b - a) * t));
		return glm::dot(offset, offset) <= radius_squared;
	});
}

auto replaceId(Volume& volume, uint8_t from, uint8_t to) -> EditResult {
	ZoneScoped;
	EditResult result;
	if (from == to) {
		return result;
	}

	const WriteBrush brush {.id = to, .mode = WriteMode::match, .match_id = from};
	for (uint32_t index = 0; index < volume.brickCount(); ++index) {
		const glm::ivec3 brick = volume.brickAtIndex(index);
		const BrickEntry entry = volume.entryAt(brick);
		if (entry.tag() == BrickTag::uniform) {
			if (entry.payload() == from) {
				volume.setBrickUniform(brick, to);
				result.changed += k_brick_voxel_count;
			}
			continue;
		}
		if (entry.tag() == BrickTag::empty && from != k_empty_palette_index) {
			continue;
		}
		const EditBounds bounds {brick * k_dim, brick * k_dim + (k_dim - 1)};
		result += fillShape(volume, bounds, brush, [](glm::vec3) { return true; });
		if (result.pool_exhausted) {
			break;
		}
	}
	return result;
}

auto slice(Volume& volume, glm::vec3 point, glm::vec3 normal, int side) -> EditResult {
	ZoneScoped;
	EditResult result;
	if (glm::dot(normal, normal) <= 0.0f || side == 0) {
		return result;
	}

	const glm::vec3 direction = glm::normalize(normal) * (side > 0 ? 1.0f : -1.0f);
	const auto cut = [&](glm::vec3 p) { return glm::dot(p - point, direction) > 0.0f; };
	const WriteBrush brush {.id = k_empty_palette_index, .mode = WriteMode::solid_only};

	for (uint32_t index = 0; index < volume.brickCount(); ++index) {
		const glm::ivec3 brick = volume.brickAtIndex(index);
		const BrickOccupancy* occupancy = volume.occupancyPointer(brick);
		if (occupancy == nullptr) {
			continue;
		}

		int cut_corners = 0;
		for (int corner = 0; corner < 8; ++corner) {
			const glm::ivec3 voxel =
			    brick * k_dim +
			    glm::ivec3((corner & 1) != 0 ? k_dim - 1 : 0, (corner & 2) != 0 ? k_dim - 1 : 0, (corner & 4) != 0 ? k_dim - 1 : 0);
			cut_corners += cut(centreOf(voxel)) ? 1 : 0;
		}
		if (cut_corners == 0) {
			continue;
		}
		if (cut_corners == 8) {
			result.changed += popCount(*occupancy);
			volume.setBrickUniform(brick, k_empty_palette_index);
			continue;
		}
		result += fillShape(volume, {brick * k_dim, brick * k_dim + (k_dim - 1)}, brush, cut);
	}
	return result;
}

auto collectFace(const Volume& volume, glm::ivec3 start, glm::ivec3 normal, size_t limit) -> std::vector<glm::ivec3> {
	ZoneScoped;
	std::vector<glm::ivec3> face;
	const auto exposed = [&](glm::ivec3 voxel) { return volume.isSolidAt(voxel) && !volume.isSolidAt(voxel + normal); };
	if (normal == glm::ivec3(0) || !exposed(start)) {
		return face;
	}

	const int axis = normal.x != 0 ? 0 : (normal.y != 0 ? 1 : 2);
	glm::ivec3 u(0);
	glm::ivec3 v(0);
	u[(axis + 1) % 3] = 1;
	v[(axis + 2) % 3] = 1;
	const std::array<glm::ivec3, 4> steps {u, -u, v, -v};

	const glm::ivec3 dims = glm::ivec3(volume.voxelDims());
	const auto key = [&](glm::ivec3 voxel) {
		return static_cast<uint64_t>(voxel.x) + (static_cast<uint64_t>(voxel.y) * static_cast<uint64_t>(dims.x)) +
		       (static_cast<uint64_t>(voxel.z) * static_cast<uint64_t>(dims.x) * static_cast<uint64_t>(dims.y));
	};

	std::unordered_set<uint64_t> seen {key(start)};
	std::deque<glm::ivec3> frontier {start};
	while (!frontier.empty() && face.size() < limit) {
		const glm::ivec3 voxel = frontier.front();
		frontier.pop_front();
		face.push_back(voxel);
		for (const glm::ivec3& step : steps) {
			const glm::ivec3 next = voxel + step;
			if (!volume.containsVoxel(next) || !exposed(next) || !seen.insert(key(next)).second) {
				continue;
			}
			frontier.push_back(next);
		}
	}
	return face;
}

auto extrudeFace(Volume& volume, std::span<const glm::ivec3> face, glm::ivec3 normal, int distance) -> EditResult {
	ZoneScoped;
	EditResult result;
	if (distance == 0 || normal == glm::ivec3(0)) {
		return result;
	}

	std::vector<uint8_t> ids;
	ids.reserve(face.size());
	for (const glm::ivec3& voxel : face) {
		ids.push_back(volume.materialAt(voxel));
	}

	std::unordered_set<uint64_t> touched_bricks;
	std::vector<glm::ivec3> touched;
	const glm::ivec3 brick_dims = glm::ivec3(volume.brickDims());
	const auto touch = [&](glm::ivec3 voxel) {
		const glm::ivec3 brick = brickOf(voxel);
		const uint64_t slot = static_cast<uint64_t>(brick.x) + (static_cast<uint64_t>(brick.y) * brick_dims.x) +
		                      (static_cast<uint64_t>(brick.z) * brick_dims.x * brick_dims.y);
		if (touched_bricks.insert(slot).second) {
			touched.push_back(brick);
		}
	};

	for (size_t i = 0; i < face.size(); ++i) {
		if (distance > 0) {
			for (int step = 1; step <= distance; ++step) {
				const glm::ivec3 voxel = face[i] + (normal * step);
				if (!volume.containsVoxel(voxel) || volume.isSolidAt(voxel)) {
					break;
				}
				if (writeVoxel(volume, voxel, ids[i], result)) {
					touch(voxel);
				}
				if (result.pool_exhausted) {
					return result;
				}
			}
		} else {
			for (int step = 0; step < -distance; ++step) {
				const glm::ivec3 voxel = face[i] - (normal * step);
				if (!volume.containsVoxel(voxel)) {
					break;
				}
				writeVoxel(volume, voxel, k_empty_palette_index, result);
				if (result.pool_exhausted) {
					return result;
				}
			}
		}
	}

	for (const glm::ivec3& brick : touched) {
		volume.tryCollapseUniform(brick);
	}
	return result;
}

auto copyRegion(const Volume& volume, glm::ivec3 a, glm::ivec3 b) -> Region {
	const EditBounds bounds = boxBounds(a, b);
	Region region;
	region.size = bounds.max - bounds.min + 1;
	region.ids.assign(static_cast<size_t>(region.size.x) * region.size.y * region.size.z, k_empty_palette_index);

	size_t i = 0;
	for (int32_t z = bounds.min.z; z <= bounds.max.z; ++z) {
		for (int32_t y = bounds.min.y; y <= bounds.max.y; ++y) {
			for (int32_t x = bounds.min.x; x <= bounds.max.x; ++x) {
				region.ids[i++] = volume.materialAt({x, y, z});
			}
		}
	}
	return region;
}

auto pasteRegion(Volume& volume, const Region& region, glm::ivec3 at, WriteMode mode, uint8_t match_id) -> EditResult {
	ZoneScoped;
	EditResult result;
	std::unordered_set<uint64_t> touched_bricks;
	std::vector<glm::ivec3> touched;
	const glm::ivec3 brick_dims = glm::ivec3(volume.brickDims());

	size_t i = 0;
	for (int32_t z = 0; z < region.size.z; ++z) {
		for (int32_t y = 0; y < region.size.y; ++y) {
			for (int32_t x = 0; x < region.size.x; ++x, ++i) {
				const uint8_t id = region.ids[i];
				const glm::ivec3 voxel = at + glm::ivec3(x, y, z);
				if (id == k_empty_palette_index || !volume.containsVoxel(voxel) || !canWrite(volume.materialAt(voxel), mode, match_id)) {
					continue;
				}
				if (writeVoxel(volume, voxel, id, result)) {
					const glm::ivec3 brick = brickOf(voxel);
					const uint64_t slot = static_cast<uint64_t>(brick.x) + (static_cast<uint64_t>(brick.y) * brick_dims.x) +
					                      (static_cast<uint64_t>(brick.z) * brick_dims.x * brick_dims.y);
					if (touched_bricks.insert(slot).second) {
						touched.push_back(brick);
					}
				}
				if (result.pool_exhausted) {
					return result;
				}
			}
		}
	}

	for (const glm::ivec3& brick : touched) {
		volume.tryCollapseUniform(brick);
	}
	return result;
}

auto stampVolume(
    Volume& target, const Volume& piece, const LatticePlacement& placement, const PaletteRemapTable& remap, WriteMode mode,
    uint8_t match_id
) -> EditResult {
	ZoneScoped;
	EditResult result;
	if (&target == &piece) {
		return result;
	}

	std::unordered_set<uint64_t> touched_bricks;
	std::vector<glm::ivec3> touched;
	const glm::ivec3 target_dims = glm::ivec3(target.brickDims());

	for (uint32_t index = 0; index < piece.brickCount(); ++index) {
		const glm::ivec3 brick = piece.brickAtIndex(index);
		const BrickEntry entry = piece.entryAt(brick);
		if (entry.tag() == BrickTag::empty) {
			continue;
		}

		for (uint32_t i = 0; i < k_brick_voxel_count; ++i) {
			uint8_t material = 0;
			if (entry.tag() == BrickTag::uniform) {
				material = static_cast<uint8_t>(entry.payload());
			} else {
				material = piece.pool()->material(entry.payload())[i];
			}
			const uint8_t mapped = remap[material];
			if (mapped == k_empty_palette_index) {
				continue;
			}

			const BrickCoord local = localFromIndex(i);
			const glm::ivec3 landed = placeVoxel(placement, brick * k_dim + glm::ivec3(local.x, local.y, local.z));
			if (!target.containsVoxel(landed) || !canWrite(target.materialAt(landed), mode, match_id)) {
				continue;
			}
			if (writeVoxel(target, landed, mapped, result)) {
				const glm::ivec3 landed_brick = brickOf(landed);
				const uint64_t slot = static_cast<uint64_t>(landed_brick.x) + (static_cast<uint64_t>(landed_brick.y) * target_dims.x) +
				                      (static_cast<uint64_t>(landed_brick.z) * target_dims.x * target_dims.y);
				if (touched_bricks.insert(slot).second) {
					touched.push_back(landed_brick);
				}
			}
			if (result.pool_exhausted) {
				return result;
			}
		}
	}

	for (const glm::ivec3& brick : touched) {
		target.tryCollapseUniform(brick);
	}
	return result;
}

auto snapOrientation(const glm::mat3& rotation) noexcept -> LatticeOrientation {
	// Greedy, alberto would be proud
	LatticeOrientation out;
	std::array<bool, 3> row_used {false, false, false};
	std::array<bool, 3> column_used {false, false, false};
	for (int pass = 0; pass < 3; ++pass) {
		int best_row = -1;
		int best_column = -1;
		float best = -1.0f;
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				const float value = std::abs(rotation[column][row]);
				if (!row_used[row] && !column_used[column] && value > best) {
					best = value;
					best_row = row;
					best_column = column;
				}
			}
		}
		// illegal matrices are false
		if (best_row < 0) {
			return LatticeOrientation {};
		}
		row_used[best_row] = true;
		column_used[best_column] = true;
		out.source[best_row] = static_cast<uint8_t>(best_column);
		out.flip[best_row] = rotation[best_column][best_row] < 0.0f;
	}
	return out;
}

auto dominantAxis(glm::vec3 normal) noexcept -> glm::ivec3 {
	const glm::vec3 magnitude = glm::abs(normal);
	if (magnitude.x == 0.0f && magnitude.y == 0.0f && magnitude.z == 0.0f) {
		return glm::ivec3(0);
	}
	glm::ivec3 out(0);
	if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
		out.x = normal.x > 0.0f ? 1 : -1;
	} else if (magnitude.y >= magnitude.z) {
		out.y = normal.y > 0.0f ? 1 : -1;
	} else {
		out.z = normal.z > 0.0f ? 1 : -1;
	}
	return out;
}

}
