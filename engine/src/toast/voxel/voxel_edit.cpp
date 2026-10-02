#include "voxel_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <optional>
#include <tracy/Tracy.hpp>
#include <unordered_set>
#include <utility>

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
	return {glm::floor(value)};
}

/** The axis a face normal points along */
[[nodiscard]]
auto faceAxis(glm::ivec3 normal) noexcept -> int {
	if (normal.x != 0) {
		return 0;
	}
	return normal.y != 0 ? 1 : 2;
}

/** The axis with the smallest value, ties go to the later axis */
[[nodiscard]]
auto minAxis(glm::vec3 value) noexcept -> int {
	if (value.x < value.y) {
		return value.x < value.z ? 0 : 2;
	}
	return value.y < value.z ? 1 : 2;
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

/**
 * Writes brush into every voxel whose centre is inside
 */
template<bool Convex = false, typename Inside>
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
				const BrickEntry entry = volume.entryAt(brick);
				if (!brickMayWrite(entry, brush)) {
					continue;
				}

				const glm::ivec3 lo = glm::max(bounds.min, brick * k_dim);
				const glm::ivec3 hi = glm::min(bounds.max, brick * k_dim + (k_dim - 1));

				if constexpr (Convex) {
					const bool whole = lo == brick * k_dim && hi == brick * k_dim + (k_dim - 1);
					if (whole && brush.mode == WriteMode::replace) {
						bool all_inside = true;
						for (int corner = 0; corner < 8 && all_inside; ++corner) {
							const glm::ivec3 voxel(
							    (corner & 1) != 0 ? hi.x : lo.x, (corner & 2) != 0 ? hi.y : lo.y, (corner & 4) != 0 ? hi.z : lo.z
							);
							all_inside = inside(centreOf(voxel));
						}
						if (all_inside) {
							const BrickEntry filled =
							    brush.id == k_empty_palette_index ? BrickEntry {} : BrickEntry::make(BrickTag::uniform, brush.id);
							if (entry != filled) {
								volume.setBrickUniform(brick, brush.id);
								result.changed += k_brick_voxel_count;
							}
							continue;
						}
					}
				}

				// Reads and writes go directly to the brick
				std::optional<Volume::WritableBrick> write;
				bool touched = false;
				const auto read = [&](uint32_t local) -> uint8_t {
					if (write.has_value()) {
						return write->material[local];
					}
					switch (entry.tag()) {
						case BrickTag::empty: return k_empty_palette_index;
						case BrickTag::uniform: return static_cast<uint8_t>(entry.payload());
						default: return std::as_const(*volume.pool()).material(entry.payload())[local];
					}
				};
				for (int32_t z = lo.z; z <= hi.z; ++z) {
					for (int32_t y = lo.y; y <= hi.y; ++y) {
						for (int32_t x = lo.x; x <= hi.x; ++x) {
							const glm::ivec3 voxel {x, y, z};
							if (!inside(centreOf(voxel))) {
								continue;
							}
							const uint32_t local =
							    localIndex(static_cast<uint32_t>(x & 7), static_cast<uint32_t>(y & 7), static_cast<uint32_t>(z & 7));
							const uint8_t current = read(local);
							if (current == brush.id || !canWrite(current, brush.mode, brush.match_id)) {
								continue;
							}
							if (!write.has_value()) {
								write = volume.beginBrickWrite(brick);
								if (!write.has_value()) {
									result.pool_exhausted = true;
									return result;
								}
							}
							write->material[local] = brush.id;
							setSolid(*write->occupancy, local, brush.id != k_empty_palette_index);
							touched = true;
							++result.changed;
						}
					}
				}
				if (write.has_value()) {
					volume.finishBrickWrite(brick, touched);
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
	return fillShape<true>(volume, sphereBounds(center, radius), brush, [&](glm::vec3 p) {
		const glm::vec3 offset = p - center;
		return glm::dot(offset, offset) <= radius_squared;
	});
}

auto fillCylinder(Volume& volume, glm::vec3 a, glm::vec3 b, float radius, const WriteBrush& brush) -> EditResult {
	const float radius_squared = radius * radius;
	return fillShape<true>(volume, segmentBounds(a, b, radius), brush, [&](glm::vec3 p) {
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
	return fillShape<true>(volume, segmentBounds(a, b, radius), brush, [&](glm::vec3 p) {
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

	const int axis = faceAxis(normal);
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

namespace {

[[nodiscard]]
auto sourceVoxel(const LatticePlacement& placement, glm::ivec3 landed) noexcept -> glm::ivec3 {
	glm::ivec3 out;
	for (int axis = 0; axis < 3; ++axis) {
		const int32_t along = placement.orientation.flip[static_cast<size_t>(axis)] ? placement.offset[axis] - landed[axis] - 1
		                                                                            : landed[axis] - placement.offset[axis];
		out[placement.orientation.source[static_cast<size_t>(axis)]] = along;
	}
	return out;
}

[[nodiscard]]
auto sourceBox(const LatticePlacement& placement, const EditBounds& landed) noexcept -> EditBounds {
	const glm::ivec3 a = sourceVoxel(placement, landed.min);
	const glm::ivec3 b = sourceVoxel(placement, landed.max);
	return {glm::min(a, b), glm::max(a, b)};
}

struct SourceSummary {
	bool empty = true;      ///< No solid voxel at all
	bool full = true;       ///< Every voxel solid
	bool uniform = true;    ///< Every brick uniform with the same id
	uint8_t id = k_empty_palette_index;
};

[[nodiscard]]
auto summarise(const Volume& piece, const EditBounds& box) noexcept -> SourceSummary {
	SourceSummary out;
	bool seen = false;
	const glm::ivec3 first = brickOf(box.min);
	const glm::ivec3 last = brickOf(box.max);
	for (int32_t bz = first.z; bz <= last.z; ++bz) {
		for (int32_t by = first.y; by <= last.y; ++by) {
			for (int32_t bx = first.x; bx <= last.x; ++bx) {
				const BrickEntry entry = piece.entryAt({bx, by, bz});
				uint8_t id = k_empty_palette_index;
				switch (entry.tag()) {
					case BrickTag::empty: out.full = false; break;
					case BrickTag::uniform:
						out.empty = false;
						id = static_cast<uint8_t>(entry.payload());
						break;
					default: {
						const BrickOccupancy& occupancy = std::as_const(*piece.pool()).occupancy(entry.payload());
						out.empty = out.empty && isEmpty(occupancy);
						out.full = out.full && isFull(occupancy);
						out.uniform = false;
						break;
					}
				}
				if (!seen) {
					out.id = id;
					seen = true;
				} else if (id != out.id) {
					out.uniform = false;
				}
			}
		}
	}
	return out;
}

[[nodiscard]]
auto wholeBrick(const EditBounds& box, glm::ivec3 brick) noexcept -> bool {
	return box.min == brick * k_dim && box.max == brick * k_dim + (k_dim - 1);
}

class BrickWriter {
public:
	BrickWriter(Volume& target, glm::ivec3 brick) : m_target(target), m_brick(brick), m_entry(target.entryAt(brick)) { }

	BrickWriter(const BrickWriter&) = delete;
	auto operator=(const BrickWriter&) -> BrickWriter& = delete;

	~BrickWriter() {
		if (m_write.has_value()) {
			m_target.finishBrickWrite(m_brick, m_changed);
		}
	}

	[[nodiscard]]
	auto entry() const noexcept -> BrickEntry {
		return m_entry;
	}

	[[nodiscard]]
	auto read(uint32_t local) const -> uint8_t {
		if (m_write.has_value()) {
			return m_write->material[local];
		}
		switch (m_entry.tag()) {
			case BrickTag::empty: return k_empty_palette_index;
			case BrickTag::uniform: return static_cast<uint8_t>(m_entry.payload());
			default: return std::as_const(*m_target.pool()).material(m_entry.payload())[local];
		}
	}

	/// @returns false when the pool ran out
	auto write(uint32_t local, uint8_t id) -> bool {
		if (!m_write.has_value()) {
			m_write = m_target.beginBrickWrite(m_brick);
			if (!m_write.has_value()) {
				return false;
			}
		}
		m_write->material[local] = id;
		setSolid(*m_write->occupancy, local, id != k_empty_palette_index);
		m_changed = true;
		return true;
	}

private:
	Volume& m_target;
	glm::ivec3 m_brick;
	BrickEntry m_entry;
	std::optional<Volume::WritableBrick> m_write;
	bool m_changed = false;
};

[[nodiscard]]
auto localIndexOf(glm::ivec3 voxel) noexcept -> uint32_t {
	return localIndex(static_cast<uint32_t>(voxel.x & 7), static_cast<uint32_t>(voxel.y & 7), static_cast<uint32_t>(voxel.z & 7));
}

[[nodiscard]]
auto landedOnTarget(const Volume& target, const Volume& piece, const LatticePlacement& placement) noexcept -> EditBounds {
	const EditBounds landed = placedBounds(piece.brickDims(), placement);
	if (landed.empty()) {
		return landed;
	}
	return clipToVolume(target, landed);
}

void fillWholeBrick(Volume& target, glm::ivec3 brick, BrickEntry before, uint8_t id, EditResult& result) {
	const BrickEntry after = id == k_empty_palette_index ? BrickEntry {} : BrickEntry::make(BrickTag::uniform, id);
	if (before != after) {
		target.setBrickUniform(brick, id);
		result.changed += k_brick_voxel_count;
	}
}

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
	const EditBounds landed = landedOnTarget(target, piece, placement);
	if (landed.empty()) {
		return result;
	}

	// Walks the target une brick at a time
	const glm::ivec3 first = brickOf(landed.min);
	const glm::ivec3 last = brickOf(landed.max);
	for (int32_t bz = first.z; bz <= last.z; ++bz) {
		for (int32_t by = first.y; by <= last.y; ++by) {
			for (int32_t bx = first.x; bx <= last.x; ++bx) {
				const glm::ivec3 brick {bx, by, bz};
				const EditBounds box {glm::max(landed.min, brick * k_dim), glm::min(landed.max, brick * k_dim + (k_dim - 1))};
				const SourceSummary source = summarise(piece, sourceBox(placement, box));
				if (source.empty) {
					continue;
				}
				BrickWriter writer(target, brick);

				// Every voxel of the brick gets the same id
				if (source.uniform && wholeBrick(box, brick)) {
					const uint8_t mapped = remap[source.id];
					if (mapped == k_empty_palette_index) {
						continue;
					}
					const BrickEntry before = writer.entry();
					if (mode == WriteMode::replace) {
						fillWholeBrick(target, brick, before, mapped, result);
						continue;
					}
					if (!before.isPooled()) {
						const uint8_t current =
						    before.tag() == BrickTag::uniform ? static_cast<uint8_t>(before.payload()) : k_empty_palette_index;
						if (canWrite(current, mode, match_id)) {
							fillWholeBrick(target, brick, before, mapped, result);
						}
						continue;
					}
				}

				for (int32_t z = box.min.z; z <= box.max.z; ++z) {
					for (int32_t y = box.min.y; y <= box.max.y; ++y) {
						for (int32_t x = box.min.x; x <= box.max.x; ++x) {
							const glm::ivec3 at {x, y, z};
							const uint8_t mapped = remap[piece.materialAt(sourceVoxel(placement, at))];
							if (mapped == k_empty_palette_index) {
								continue;
							}
							const uint32_t local = localIndexOf(at);
							const uint8_t current = writer.read(local);
							if (current == mapped || !canWrite(current, mode, match_id)) {
								continue;
							}
							if (!writer.write(local, mapped)) {
								result.pool_exhausted = true;
								return result;
							}
							++result.changed;
						}
					}
				}
			}
		}
	}
	return result;
}

auto carveVolume(Volume& target, const Volume& piece, const LatticePlacement& placement) -> EditResult {
	ZoneScoped;
	EditResult result;
	if (&target == &piece) {
		return result;
	}
	const EditBounds landed = landedOnTarget(target, piece, placement);
	if (landed.empty()) {
		return result;
	}

	const glm::ivec3 first = brickOf(landed.min);
	const glm::ivec3 last = brickOf(landed.max);
	for (int32_t bz = first.z; bz <= last.z; ++bz) {
		for (int32_t by = first.y; by <= last.y; ++by) {
			for (int32_t bx = first.x; bx <= last.x; ++bx) {
				const glm::ivec3 brick {bx, by, bz};
				const BrickEntry before = target.entryAt(brick);
				if (before.tag() == BrickTag::empty) {
					continue;
				}
				const EditBounds box {glm::max(landed.min, brick * k_dim), glm::min(landed.max, brick * k_dim + (k_dim - 1))};
				const SourceSummary source = summarise(piece, sourceBox(placement, box));
				if (source.empty) {
					continue;
				}
				if (source.full && wholeBrick(box, brick)) {
					if (const BrickOccupancy* occupancy = target.occupancyPointer(brick)) {
						result.changed += popCount(*occupancy);
					}
					target.setBrickUniform(brick, k_empty_palette_index);
					continue;
				}

				BrickWriter writer(target, brick);
				for (int32_t z = box.min.z; z <= box.max.z; ++z) {
					for (int32_t y = box.min.y; y <= box.max.y; ++y) {
						for (int32_t x = box.min.x; x <= box.max.x; ++x) {
							const glm::ivec3 at {x, y, z};
							const uint32_t local = localIndexOf(at);
							if (writer.read(local) == k_empty_palette_index || !piece.isSolidAt(sourceVoxel(placement, at))) {
								continue;
							}
							if (!writer.write(local, k_empty_palette_index)) {
								result.pool_exhausted = true;
								return result;
							}
							++result.changed;
						}
					}
				}
			}
		}
	}
	return result;
}

auto fillRoundBox(Volume& volume, glm::ivec3 a, glm::ivec3 b, float radius, const WriteBrush& brush) -> EditResult {
	const EditBounds bounds = boxBounds(a, b);
	const glm::vec3 half = glm::vec3(bounds.max - bounds.min + 1) * 0.5f;
	const float r = std::clamp(radius, 0.0f, std::min({half.x, half.y, half.z}));
	if (r <= 0.0f) {
		return fillBox(volume, a, b, brush);
	}

	const glm::vec3 centre = glm::vec3(bounds.min) + half;
	return fillShape<true>(volume, bounds, brush, [&](glm::vec3 p) {
		const glm::vec3 q = glm::abs(p - centre) - half + r;
		const float outside = glm::length(glm::max(q, glm::vec3(0.0f)));
		const float inside = std::min(std::max({q.x, q.y, q.z}), 0.0f);
		return outside + inside - r <= 0.0f;
	});
}

auto fillEllipsoid(Volume& volume, glm::ivec3 a, glm::ivec3 b, const WriteBrush& brush) -> EditResult {
	const EditBounds bounds = boxBounds(a, b);
	const glm::vec3 half = glm::vec3(bounds.max - bounds.min + 1) * 0.5f;
	const glm::vec3 centre = glm::vec3(bounds.min) + half;
	return fillShape<true>(volume, bounds, brush, [&](glm::vec3 p) {
		const glm::vec3 unit = (p - centre) / half;
		return glm::dot(unit, unit) <= 1.0f;
	});
}

namespace {

[[nodiscard]]
auto keptByPlanes(std::span<const glm::vec4> planes, glm::vec3 p) noexcept -> bool {
	return std::ranges::all_of(planes, [&](const glm::vec4& plane) { return glm::dot(glm::vec3(plane), p) + plane.w >= 0.0f; });
}

}

auto clipByPlanes(Volume& volume, std::span<const glm::vec4> planes) -> EditResult {
	ZoneScoped;
	EditResult result;
	if (planes.empty()) {
		return result;
	}

	const WriteBrush brush {.id = k_empty_palette_index, .mode = WriteMode::solid_only};
	for (uint32_t index = 0; index < volume.brickCount(); ++index) {
		const glm::ivec3 brick = volume.brickAtIndex(index);
		const BrickOccupancy* occupancy = volume.occupancyPointer(brick);
		if (occupancy == nullptr || isEmpty(*occupancy)) {
			continue;
		}

		// A brick is a convex box so its corner centres decide it unless a plane crosses it
		bool all_kept = true;
		bool any_plane_drops_all = false;
		for (const glm::vec4& plane : planes) {
			int kept = 0;
			for (int corner = 0; corner < 8; ++corner) {
				const glm::ivec3 voxel =
				    brick * k_dim +
				    glm::ivec3((corner & 1) != 0 ? k_dim - 1 : 0, (corner & 2) != 0 ? k_dim - 1 : 0, (corner & 4) != 0 ? k_dim - 1 : 0);
				kept += glm::dot(glm::vec3(plane), centreOf(voxel)) + plane.w >= 0.0f ? 1 : 0;
			}
			all_kept = all_kept && kept == 8;
			any_plane_drops_all = any_plane_drops_all || kept == 0;
		}
		if (all_kept) {
			continue;
		}
		if (any_plane_drops_all) {
			result.changed += popCount(*occupancy);
			volume.setBrickUniform(brick, k_empty_palette_index);
			continue;
		}
		result += fillShape(volume, {brick * k_dim, brick * k_dim + (k_dim - 1)}, brush, [&](glm::vec3 p) {
			return !keptByPlanes(planes, p);
		});
		if (result.pool_exhausted) {
			break;
		}
	}
	return result;
}

auto complementPlane(glm::vec4 plane) noexcept -> glm::vec4 {
	// Voxel centers sit on half voxels
	constexpr float nudge = 1e-4f;
	return {-glm::vec3(plane), -plane.w - nudge};
}

namespace {

[[nodiscard]]
auto anchorShift(int32_t size, int32_t tile, TileAnchor anchor) noexcept -> int32_t {
	const int32_t spare = (tile - (size % tile)) % tile;
	switch (anchor) {
		case TileAnchor::min: return 0;
		case TileAnchor::center: return spare / 2;
		case TileAnchor::max: return spare;
	}
	return 0;
}

[[nodiscard]]
auto floorMod(int32_t value, int32_t divisor) noexcept -> int32_t {
	const int32_t out = value % divisor;
	return out < 0 ? out + divisor : out;
}

[[nodiscard]]
auto floorDiv(int32_t value, int32_t divisor) noexcept -> int32_t {
	return (value - floorMod(value, divisor)) / divisor;
}

}

auto tileVolume(
    Volume& target, const Volume& pattern, glm::ivec3 a, glm::ivec3 b, const TileOptions& options, const PaletteRemapTable& remap,
    WriteMode mode, uint8_t match_id
) -> EditResult {
	ZoneScoped;
	EditResult result;
	const std::optional<EditBounds> solid = occupiedBounds(pattern);
	const EditBounds box = clipToVolume(target, boxBounds(a, b));
	if (!solid.has_value() || box.empty()) {
		return result;
	}

	const glm::ivec3 full_size = boxBounds(a, b).max - boxBounds(a, b).min + 1;
	const glm::ivec3 tile = solid->max - solid->min + 1;

	// Per axis tile count and shift
	glm::ivec3 count {1};
	glm::ivec3 shift {0};
	for (int axis = 0; axis < 3; ++axis) {
		if (options.fit == TileFit::stretch) {
			count[axis] = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(full_size[axis]) / tile[axis])));
		} else {
			const TileAnchor anchor = options.fit == TileFit::center ? TileAnchor::center : options.anchor[static_cast<size_t>(axis)];
			shift[axis] = anchorShift(full_size[axis], tile[axis], anchor);
		}
	}

	const glm::ivec3 origin = boxBounds(a, b).min;
	std::unordered_set<uint64_t> touched_bricks;
	std::vector<glm::ivec3> touched;
	const glm::ivec3 brick_dims = glm::ivec3(target.brickDims());

	for (int32_t z = box.min.z; z <= box.max.z; ++z) {
		for (int32_t y = box.min.y; y <= box.max.y; ++y) {
			for (int32_t x = box.min.x; x <= box.max.x; ++x) {
				const glm::ivec3 voxel {x, y, z};
				glm::ivec3 source;
				for (int axis = 0; axis < 3; ++axis) {
					int32_t along = voxel[axis] - origin[axis];
					if (options.fit == TileFit::stretch) {
						// Scale into count whole tiles, nearest neighbour
						along = static_cast<int32_t>((static_cast<int64_t>(along) * count[axis] * tile[axis]) / full_size[axis]);
					}
					along += shift[axis] + options.offset[axis];
					int32_t local = floorMod(along, tile[axis]);
					if (options.mirror[axis] && (floorDiv(along, tile[axis]) & 1) != 0) {
						local = tile[axis] - 1 - local;
					}
					source[axis] = solid->min[axis] + local;
				}

				const uint8_t mapped = remap[pattern.materialAt(source)];
				if (mapped == k_empty_palette_index || !canWrite(target.materialAt(voxel), mode, match_id)) {
					continue;
				}
				if (writeVoxel(target, voxel, mapped, result)) {
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
		target.tryCollapseUniform(brick);
	}
	return result;
}

auto floodFill(Volume& volume, glm::ivec3 seed, uint8_t id) -> EditResult {
	ZoneScoped;
	EditResult result;
	if (!volume.containsVoxel(seed) || id == k_empty_palette_index) {
		return result;
	}
	const uint8_t from = volume.materialAt(seed);
	if (from == k_empty_palette_index || from == id) {
		return result;
	}

	const std::array<glm::ivec3, 6> steps {
	  glm::ivec3 { 1,  0,  0},
	  glm::ivec3 {-1,  0,  0},
	  glm::ivec3 { 0,  1,  0},
	  glm::ivec3 { 0, -1,  0},
	  glm::ivec3 { 0,  0,  1},
	  glm::ivec3 { 0,  0, -1}
	};

	// Repainting marks a voxel as visited since it no longer holds from
	std::deque<glm::ivec3> frontier {seed};
	writeVoxel(volume, seed, id, result);
	while (!frontier.empty() && !result.pool_exhausted) {
		const glm::ivec3 voxel = frontier.front();
		frontier.pop_front();
		for (const glm::ivec3& step : steps) {
			const glm::ivec3 next = voxel + step;
			if (!volume.containsVoxel(next) || volume.materialAt(next) != from) {
				continue;
			}
			if (writeVoxel(volume, next, id, result)) {
				frontier.push_back(next);
			}
		}
	}
	return result;
}

auto raycast(const Volume& volume, glm::vec3 origin, glm::vec3 direction, float max_t) -> std::optional<VolumeHit> {
	ZoneScoped;
	if (glm::dot(direction, direction) <= 0.0f) {
		return std::nullopt;
	}

	// Clip the ray to the volume box first so a ray from far away starts at the surface
	const glm::vec3 box_max = glm::vec3(volume.voxelDims());
	float t_enter = 0.0f;
	float t_exit = max_t;
	int32_t enter_axis = -1;
	for (int axis = 0; axis < 3; ++axis) {
		if (std::abs(direction[axis]) < 1e-12f) {
			if (origin[axis] < 0.0f || origin[axis] > box_max[axis]) {
				return std::nullopt;
			}
			continue;
		}
		float t0 = (0.0f - origin[axis]) / direction[axis];
		float t1 = (box_max[axis] - origin[axis]) / direction[axis];
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		if (t0 > t_enter) {
			t_enter = t0;
			enter_axis = axis;
		}
		t_exit = std::min(t_exit, t1);
	}
	if (t_enter > t_exit) {
		return std::nullopt;
	}

	const glm::vec3 start = origin + direction * t_enter;
	glm::ivec3 voxel = glm::clamp(floorToVoxel(start), glm::ivec3(0), glm::ivec3(volume.voxelDims()) - 1);
	const glm::ivec3 step = glm::ivec3(glm::sign(direction));
	glm::vec3 t_max_axis;
	glm::vec3 t_delta;
	for (int axis = 0; axis < 3; ++axis) {
		if (step[axis] == 0) {
			t_max_axis[axis] = std::numeric_limits<float>::max();
			t_delta[axis] = std::numeric_limits<float>::max();
			continue;
		}
		const float boundary = static_cast<float>(voxel[axis] + (step[axis] > 0 ? 1 : 0));
		t_max_axis[axis] = (boundary - origin[axis]) / direction[axis];
		t_delta[axis] = std::abs(1.0f / direction[axis]);
	}

	glm::ivec3 normal(0);
	if (enter_axis >= 0) {
		normal[enter_axis] = -step[enter_axis];
	}
	float t = t_enter;
	while (t <= t_exit && volume.containsVoxel(voxel)) {
		if (volume.isSolidAt(voxel)) {
			return VolumeHit {.voxel = voxel, .normal = normal, .t = t};
		}
		const int axis = minAxis(t_max_axis);
		t = t_max_axis[axis];
		t_max_axis[axis] += t_delta[axis];
		voxel[axis] += step[axis];
		normal = glm::ivec3(0);
		normal[axis] = -step[axis];
	}
	return std::nullopt;
}

auto idHistogram(const Volume& volume) -> std::array<uint32_t, k_palette_size> {
	std::array<uint32_t, k_palette_size> counts {};
	for (uint32_t index = 0; index < volume.brickCount(); ++index) {
		const glm::ivec3 brick = volume.brickAtIndex(index);
		const BrickEntry entry = volume.entryAt(brick);
		if (entry.tag() == BrickTag::empty) {
			continue;
		}
		if (entry.tag() == BrickTag::uniform) {
			counts[entry.payload() & 0xFFu] += k_brick_voxel_count;
			continue;
		}
		const auto material = volume.pool()->material(entry.payload());
		for (uint32_t i = 0; i < k_brick_voxel_count; ++i) {
			++counts[material[i]];
		}
	}
	counts[k_empty_palette_index] = 0;
	return counts;
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
