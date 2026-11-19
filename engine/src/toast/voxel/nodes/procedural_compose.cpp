#include "procedural_compose.hpp"

#include <algorithm>
#include <bitset>
#include <cstring>
#include <limits>
#include <toast/voxel/procedural_pool.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <tuple>
#include <unordered_map>

namespace toast::procedural {

namespace {

struct ProjectionAxes {
	int h;
	int v;
	int depth;
	int toward;
};

constexpr std::array<ProjectionAxes, 3> k_projection_views {
  ProjectionAxes {0, 2, 1,  1},
   ProjectionAxes {1, 2, 0, -1},
   ProjectionAxes {0, 1, 2, -1}
};

void projectShape(
    const voxel::Volume& shape, glm::ivec3 origin, const voxel::Palette& palette, std::vector<event::VoxelProjectionData>& out
) {
	ZoneScoped;
	glm::ivec3 dims = glm::ivec3(shape.voxelDims());

	for (const auto& view : k_projection_views) {
		event::VoxelProjectionData projection;
		projection.min_h = origin[view.h];
		projection.min_v = origin[view.v];
		projection.width = static_cast<uint32_t>(dims[view.h]);
		projection.height = static_cast<uint32_t>(dims[view.v]);
		projection.colors.assign(static_cast<size_t>(projection.width) * projection.height * 4, 0);
		projection.depths.assign(static_cast<size_t>(projection.width) * projection.height, std::numeric_limits<int16_t>::max());
		projection.edges_h.assign(static_cast<size_t>(projection.width + 1) * projection.height * 4, 0);
		projection.edges_v.assign(static_cast<size_t>(projection.width) * (projection.height + 1) * 4, 0);
		out.push_back(std::move(projection));
	}

	// Outlines
	for (uint32_t index = 0; index < shape.brickCount(); ++index) {
		glm::ivec3 brick = shape.brickAtIndex(index);
		voxel::BrickEntry entry = shape.entryAt(brick);

		if (entry.tag() == voxel::BrickTag::empty) {
			continue;
		}
		const auto& occupancy = *shape.occupancyPointer(brick);
		voxel::BrickNeighbourhood around = shape.neighbourhoodOf(brick);
		std::array<std::array<voxel::BrickOccupancy, 2>, 3> exposed {
		  {
		   {occupancy & ~voxel::neighboursNegX(occupancy, around.neg_x),
			   occupancy & ~voxel::neighboursPosX(occupancy, around.pos_x)},
		   {occupancy & ~voxel::neighboursNegY(occupancy, around.neg_y),
			   occupancy & ~voxel::neighboursPosY(occupancy, around.pos_y)},
		   {occupancy & ~voxel::neighboursNegZ(occupancy, around.neg_z),
			   occupancy & ~voxel::neighboursPosZ(occupancy, around.pos_z)},
		   }
		};
		bool uniform = entry.tag() == voxel::BrickTag::uniform;
		const uint8_t* materials = uniform ? nullptr : std::as_const(*shape.pool()).material(entry.payload()).data();
		glm::ivec3 corner = brick * static_cast<int32_t>(voxel::k_brick_dim);

		for (size_t v = 0; v < k_projection_views.size(); ++v) {
			event::VoxelProjectionData& projection = out[v];
			const auto& view = k_projection_views[v];
			const auto& across = exposed[static_cast<size_t>(view.h)];
			const auto& up = exposed[static_cast<size_t>(view.v)];
			for (uint32_t z = 0; z < voxel::k_brick_dim; ++z) {
				uint64_t bits = across[0][z] | across[1][z] | up[0][z] | up[1][z];
				while (bits != 0) {
					auto bit = static_cast<uint32_t>(std::countr_zero(bits));
					uint64_t mask = uint64_t {1} << bit;
					bits &= bits - 1;

					uint32_t local = (z * 64) + bit;
					uint8_t id = uniform ? static_cast<uint8_t>(entry.payload()) : materials[local];
					const voxel::PaletteEntry& color = palette.entries[id];
					glm::ivec3 cell =
					    corner + glm::ivec3(static_cast<int32_t>(bit & 7), static_cast<int32_t>(bit >> 3), static_cast<int32_t>(z));
					auto h = static_cast<size_t>(cell[view.h]);
					auto row = static_cast<size_t>(cell[view.v]);

					// See through
					auto mark = [&](std::vector<uint8_t>& edges, size_t at) {
						edges[(at * 4) + 0] = color.albedo_r;
						edges[(at * 4) + 1] = color.albedo_g;
						edges[(at * 4) + 2] = color.albedo_b;
						edges[(at * 4) + 3] = 255;
					};
					if ((across[0][z] & mask) != 0) {
						mark(projection.edges_h, h + (row * (projection.width + 1)));
					}
					if ((across[1][z] & mask) != 0) {
						mark(projection.edges_h, h + 1 + (row * (projection.width + 1)));
					}
					if ((up[0][z] & mask) != 0) {
						mark(projection.edges_v, h + (row * projection.width));
					}
					if ((up[1][z] & mask) != 0) {
						mark(projection.edges_v, h + ((row + 1) * projection.width));
					}
				}
			}
		}
	}

	// See through fill
	constexpr float k_layer_alpha = 0.1f;
	constexpr int32_t k_dim = static_cast<int32_t>(voxel::k_brick_dim);
	for (size_t v = 0; v < k_projection_views.size(); ++v) {
		event::VoxelProjectionData& projection = out[v];
		const auto& view = k_projection_views[v];
		int length = dims[view.depth];
		for (uint32_t row = 0; row < projection.height; ++row) {
			for (uint32_t h = 0; h < projection.width; ++h) {
				glm::vec3 color(0.0f);
				float alpha = 0.0f;
				uint8_t previous = voxel::k_empty_palette_index;
				int nearest = -1;
				glm::ivec3 cell;
				cell[view.h] = static_cast<int>(h);
				cell[view.v] = static_cast<int>(row);
				auto layer = [&](uint8_t id) {
					const voxel::PaletteEntry& entry = palette.entries[id];
					glm::vec3 tint = glm::vec3(entry.albedo_r, entry.albedo_g, entry.albedo_b) / 255.0f;
					color = tint * k_layer_alpha + color * (1.0f - k_layer_alpha);
					alpha = k_layer_alpha + (alpha * (1.0f - k_layer_alpha));
				};
				int step = 0;
				while (step < length) {
					cell[view.depth] = view.toward > 0 ? length - 1 - step : step;
					voxel::BrickEntry entry = shape.entryAt({cell.x >> 3, cell.y >> 3, cell.z >> 3});
					int along = cell[view.depth] & (k_dim - 1);
					int rest = view.toward > 0 ? along + 1 : k_dim - along;
					if (entry.tag() == voxel::BrickTag::empty) {
						step += rest;
						previous = voxel::k_empty_palette_index;
						continue;
					}
					if (entry.tag() == voxel::BrickTag::uniform) {
						auto id = static_cast<uint8_t>(entry.payload());
						if (id != previous) {
							layer(id);
						}
						previous = id;
						nearest = view.toward > 0 ? cell[view.depth] - (rest - 1) : cell[view.depth] + (rest - 1);
						step += rest;
						continue;
					}
					uint8_t id = shape.materialAt(cell);
					if (id != voxel::k_empty_palette_index) {
						if (id != previous) {
							layer(id);
						}
						nearest = cell[view.depth];
					}
					previous = id;
					++step;
				}
				if (alpha <= 0.0f) {
					continue;
				}
				// color is premultiplied
				size_t slot = static_cast<size_t>(h) + (static_cast<size_t>(row) * projection.width);
				for (int c = 0; c < 3; ++c) {
					projection.colors[(slot * 4) + c] =
					    static_cast<uint8_t>(std::lround(glm::clamp(color[c] / alpha, 0.0f, 1.0f) * 255.0f));
				}
				projection.colors[(slot * 4) + 3] = static_cast<uint8_t>(std::lround(alpha * 255.0f));
				projection.depths[slot] = static_cast<int16_t>(view.toward * (nearest + origin[view.depth]));
			}
		}
	}
}

template<typename Visit>
void visitSolid(const voxel::Volume& volume, glm::ivec3 brick, Visit&& visit) {
	voxel::BrickEntry entry = volume.entryAt(brick);
	if (entry.tag() == voxel::BrickTag::empty) {
		return;
	}
	glm::ivec3 corner = brick * static_cast<int32_t>(voxel::k_brick_dim);
	if (entry.tag() == voxel::BrickTag::uniform) {
		auto id = static_cast<uint8_t>(entry.payload());
		for (int32_t z = 0; std::cmp_less(z, voxel::k_brick_dim); ++z) {
			for (int32_t y = 0; std::cmp_less(y, voxel::k_brick_dim); ++y) {
				for (int32_t x = 0; std::cmp_less(x, voxel::k_brick_dim); ++x) {
					visit(corner + glm::ivec3(x, y, z), id);
				}
			}
		}
		return;
	}
	const voxel::BrickOccupancy& occupancy = *volume.occupancyPointer(brick);
	const uint8_t* materials = std::as_const(*volume.pool()).material(entry.payload()).data();
	for (uint32_t z = 0; z < voxel::k_brick_dim; ++z) {
		uint64_t bits = occupancy[z];
		while (bits != 0) {
			auto bit = static_cast<uint32_t>(std::countr_zero(bits));
			bits &= bits - 1;
			visit(
			    corner + glm::ivec3(static_cast<int32_t>(bit & 7), static_cast<int32_t>(bit >> 3), static_cast<int32_t>(z)),
			    materials[(z * 64) + bit]
			);
		}
	}
}

template<typename Part>
auto splitByOwner(
    const voxel::Volume& shape, const voxel::Volume& owner, glm::ivec3 origin, const std::vector<uint64_t>& keys,
    bool& pool_exhausted
) -> std::vector<Part> {
	ZoneScopedN("voxel::SplitByOwner");    // NOLINT

	struct Extent {
		glm::ivec3 min {std::numeric_limits<int32_t>::max()};
		glm::ivec3 max {std::numeric_limits<int32_t>::min()};
		bool any = false;
	};

	std::vector<Extent> extents(keys.size());

	// Every owner number in a brick
	auto owners_in = [&](glm::ivec3 brick) {
		std::bitset<256> seen;
		voxel::BrickEntry entry = owner.entryAt(brick);
		if (entry.tag() == voxel::BrickTag::uniform) {
			seen.set(static_cast<uint8_t>(entry.payload()));
		} else if (entry.tag() != voxel::BrickTag::empty) {
			const voxel::BrickOccupancy& occupancy = *owner.occupancyPointer(brick);
			const uint8_t* materials = std::as_const(*owner.pool()).material(entry.payload()).data();
			for (uint32_t z = 0; z < voxel::k_brick_dim; ++z) {
				uint64_t bits = occupancy[z];
				while (bits != 0) {
					auto bit = static_cast<uint32_t>(std::countr_zero(bits));
					bits &= bits - 1;
					seen.set(materials[(z * 64) + bit]);
				}
			}
		}
		seen.reset(0);
		return seen;
	};

	std::vector<std::pair<glm::ivec3, std::bitset<256>>> bricks;
	for (uint32_t index = 0; index < owner.brickCount(); ++index) {
		glm::ivec3 brick = owner.brickAtIndex(index);
		if (owner.entryAt(brick).tag() == voxel::BrickTag::empty) {
			continue;
		}
		std::bitset<256> seen = owners_in(brick);
		for (size_t id = 1; id <= keys.size(); ++id) {
			if (seen.test(id)) {
				Extent& extent = extents[id - 1];
				extent.min = glm::min(extent.min, brick);
				extent.max = glm::max(extent.max, brick);
				extent.any = true;
			}
		}
		bricks.emplace_back(brick, seen);
	}

	std::vector<Part> parts;
	std::vector<int> part_of(keys.size(), -1);
	for (size_t id = 1; id <= keys.size(); ++id) {
		const Extent& extent = extents[id - 1];
		if (!extent.any) {
			continue;
		}
		Part part;
		part.key = keys[id - 1];
		part.origin = origin + (extent.min * static_cast<int32_t>(voxel::k_brick_dim));
		part.volume = std::make_unique<voxel::Volume>(voxel::proceduralBrickPool(), glm::uvec3(extent.max - extent.min + 1));
		part_of[id - 1] = static_cast<int>(parts.size());
		parts.push_back(std::move(part));
	}

	for (const auto& [brick, seen] : bricks) {
		if (seen.count() == 1) {
			// Whole brick moves
			size_t id = 1;
			while (!seen.test(id)) {
				++id;
			}
			if (id <= keys.size()) {
				const Extent& extent = extents[id - 1];
				parts[static_cast<size_t>(part_of[id - 1])].volume->copyBrickFrom(brick - extent.min, shape, brick);
			}
			continue;
		}
		visitSolid(owner, brick, [&](glm::ivec3 cell, uint8_t id) {
			if (id == 0 || id > keys.size()) {
				return;
			}
			const Extent& extent = extents[id - 1];
			voxel::Volume::VoxelWrite write = parts[static_cast<size_t>(part_of[id - 1])].volume->setVoxel(
			    cell - (extent.min * static_cast<int32_t>(voxel::k_brick_dim)), shape.materialAt(cell)
			);
			pool_exhausted = pool_exhausted || !write.changed;
		});
	}
	return parts;
}

}

namespace {

constexpr auto k_dim = static_cast<int32_t>(voxel::k_brick_dim);

[[nodiscard]]
auto intersects(const voxel::EditBounds& a, const voxel::EditBounds& b) noexcept -> bool {
	return !a.empty() && !b.empty() && glm::all(glm::lessThanEqual(a.min, b.max)) && glm::all(glm::lessThanEqual(b.min, a.max));
}

[[nodiscard]]
auto partBricks(const VoxelPart& part) -> std::pair<glm::ivec3, glm::ivec3> {
	glm::ivec3 first = brickFloor(part.origin);
	return {first, first + glm::ivec3(part.volume->brickDims()) - 1};
}

auto growPart(VoxelPart& part, const voxel::EditBounds& bounds) -> bool {
	constexpr int32_t k_max_bricks_per_axis = 512;
	constexpr int64_t k_max_bricks = int64_t {1} << 21;

	glm::ivec3 first = brickFloor(bounds.min);
	glm::ivec3 dims = brickFloor(bounds.max) - first + 1;
	if (glm::any(glm::greaterThan(dims, glm::ivec3(k_max_bricks_per_axis))) ||
	    static_cast<int64_t>(dims.x) * dims.y * dims.z > k_max_bricks) {
		return false;
	}

	// Physics use the old volume until it builds the new one
	if (part.volume != nullptr && part.body != physics::BodyID {}) {
		part.retired.push_back(std::move(part.volume));
	}
	part.volume = std::make_unique<voxel::Volume>(voxel::runtimeBrickPool(), glm::uvec3(dims));
	part.origin = first * k_dim;
	++part.revision;
	return true;
}

[[nodiscard]]
auto slackAround(glm::ivec3 min, glm::ivec3 max, bool slack) -> glm::ivec3 {
	return slack ? glm::max(glm::ivec3(2 * k_dim), (max - min + 1) / 4) : glm::ivec3(0);
}

auto growKeeping(VoxelPart& part, const voxel::EditBounds& wanted, bool slack) -> bool {
	voxel::EditBounds bounds = wanted;
	if (part.volume != nullptr) {
		const auto [first, last] = partBricks(part);
		bounds = voxel::boundsUnion(bounds, {first * k_dim, ((last + 1) * k_dim) - 1});
	}
	glm::ivec3 room = slackAround(bounds.min, bounds.max, slack);
	VoxelPart grown;
	if (!growPart(grown, {bounds.min - room, bounds.max + room}) && !growPart(grown, bounds)) {
		return false;
	}

	// Physics use the old volume until it builds the new one
	if (part.volume != nullptr) {
		glm::ivec3 shift = brickFloor(part.origin) - brickFloor(grown.origin);
		for (uint32_t index = 0; index < part.volume->brickCount(); ++index) {
			glm::ivec3 brick = part.volume->brickAtIndex(index);
			if (part.volume->entryAt(brick).tag() != voxel::BrickTag::empty) {
				grown.volume->copyBrickFrom(brick + shift, *part.volume, brick);
			}
		}
		if (part.body != physics::BodyID {}) {
			part.retired.push_back(std::move(part.volume));
		}
	}
	part.volume = std::move(grown.volume);
	part.origin = grown.origin;
	++part.revision;
	return true;
}

void retirePart(VoxelPart& part, std::vector<std::unique_ptr<voxel::Volume>>& retired) {
	if (part.body == physics::BodyID {}) {
		return;
	}
	for (std::unique_ptr<voxel::Volume>& volume : part.retired) {
		retired.push_back(std::move(volume));
	}
	retired.push_back(std::move(part.volume));
}

auto copyBricks(VoxelPart& part, const Part* source, glm::ivec3 first, glm::ivec3 last) -> bool {
	glm::ivec3 part_first = brickFloor(part.origin);
	glm::ivec3 source_first = source != nullptr ? brickFloor(source->origin) : glm::ivec3(0);
	bool changed = false;
	for (int32_t z = first.z; z <= last.z; ++z) {
		for (int32_t y = first.y; y <= last.y; ++y) {
			for (int32_t x = first.x; x <= last.x; ++x) {
				glm::ivec3 shape_brick {x, y, z};
				glm::ivec3 brick = shape_brick - part_first;
				glm::ivec3 from = shape_brick - source_first;
				if (source != nullptr && source->volume->containsBrick(from)) {
					changed = part.volume->copyBrickFrom(brick, *source->volume, from) || changed;
				} else if (part.volume->entryAt(brick).tag() != voxel::BrickTag::empty) {
					part.volume->setBrickUniform(brick, voxel::k_empty_palette_index);
					changed = true;
				}
			}
		}
	}
	return changed;
}

}

auto Stamp::bounds() const noexcept -> voxel::EditBounds {
	return grid != nullptr ? voxel::placedBounds(grid->brickDims(), placement) : voxel::EditBounds {};
}

auto Stamp::sameAs(const Stamp& other) const noexcept -> bool {
	return grid == other.grid && placement == other.placement && kind == other.kind && mode == other.mode &&
	       match_id == other.match_id && key == other.key && remap == other.remap;
}

void assignPartKeys(Request& request) {
	std::vector<uint64_t> seen;
	const auto lump = [&](uint64_t key) -> uint64_t {
		if (std::ranges::contains(seen, key)) {
			return key;
		}
		if (seen.size() < k_max_part_keys) {
			seen.push_back(key);
			return key;
		}
		request.too_many_parts = true;
		return seen.back();
	};

	// order a compose claims them
	if (request.base != nullptr) {
		lump(0);
	}
	for (Stamp& stamp : request.stamps) {
		if (stamp.kind == PieceKind::fill) {
			stamp.key = lump(stamp.key);
		}
	}
	if (request.preview.has_value() && request.preview->kind == PieceKind::fill) {
		request.preview->key = lump(request.preview->key);
	}
}

auto compose(const Request& request) -> Result {
	ZoneScopedN("voxel::ComposeProcedural");    // NOLINT
	Result out;
	out.region = request.region;
	out.too_many_parts = request.too_many_parts;
	bool full = !request.region.has_value();
	out.has_projections = full && request.projections;

	voxel::EditBounds bounds {};
	if (full) {
		if (request.base != nullptr) {
			bounds = {glm::ivec3(0), glm::ivec3(request.base->voxelDims()) - 1};
		}
		for (const Stamp& stamp : request.stamps) {
			if (stamp.kind == PieceKind::fill) {
				bounds = voxel::boundsUnion(bounds, stamp.bounds());
			}
		}
		if (request.preview.has_value() && request.preview->kind == PieceKind::fill) {
			bounds = voxel::boundsUnion(bounds, request.preview->bounds());
		}
	} else {
		bounds = *request.region;
	}
	if (bounds.empty()) {
		return out;
	}

	glm::ivec3 first = brickFloor(bounds.min);
	glm::ivec3 last = brickFloor(bounds.max);
	glm::ivec3 origin = first * k_dim;
	voxel::EditBounds covered {origin, ((last + 1) * k_dim) - 1};
	voxel::Volume target(voxel::proceduralBrickPool(), glm::uvec3(last - first + 1));

	voxel::Volume owner(voxel::proceduralBrickPool(), target.brickDims());
	std::vector<uint64_t> owner_keys;
	auto claim = [&](uint64_t key) -> uint8_t {
		if (const auto found = std::ranges::find(owner_keys, key); found != owner_keys.end()) {
			return static_cast<uint8_t>((found - owner_keys.begin()) + 1);
		}
		if (owner_keys.size() >= k_max_part_keys) {
			out.too_many_parts = true;
			return static_cast<uint8_t>(k_max_part_keys);
		}
		owner_keys.push_back(key);
		return static_cast<uint8_t>(owner_keys.size());
	};

	auto claim_table = [](const voxel::PaletteRemapTable& remap, uint8_t id) {
		voxel::PaletteRemapTable table {};
		for (uint32_t i = 1; i < voxel::k_palette_size; ++i) {
			table[i] = remap[i] != voxel::k_empty_palette_index ? id : voxel::k_empty_palette_index;
		}
		return table;
	};

	auto claim_mode = [](voxel::WriteMode mode, uint8_t match_id) -> std::optional<voxel::WriteMode> {
		switch (mode) {
			case voxel::WriteMode::replace: return voxel::WriteMode::replace;
			case voxel::WriteMode::empty_only: return voxel::WriteMode::empty_only;
			case voxel::WriteMode::match:
				return match_id == voxel::k_empty_palette_index ? std::optional {voxel::WriteMode::empty_only} : std::nullopt;
			case voxel::WriteMode::solid_only: return std::nullopt;
		}
		return std::nullopt;
	};

	voxel::EditResult result;
	auto write = [&](const Stamp& stamp) {
		voxel::LatticePlacement indexed = stamp.placement;
		indexed.offset -= origin;
		const voxel::Volume& grid = *stamp.grid;
		switch (stamp.kind) {
			case PieceKind::fill: {
				result += voxel::stampVolume(target, grid, indexed, stamp.remap, stamp.mode, stamp.match_id);
				uint8_t id = claim(stamp.key);
				if (const std::optional<voxel::WriteMode> mode = claim_mode(stamp.mode, stamp.match_id)) {
					result += voxel::stampVolume(owner, grid, indexed, claim_table(stamp.remap, id), *mode, 0);
				}
				break;
			}
			case PieceKind::carve:
				result += voxel::carveVolume(target, grid, indexed);
				result += voxel::carveVolume(owner, grid, indexed);
				break;
			case PieceKind::paint:
				result += voxel::stampVolume(target, grid, indexed, stamp.remap, voxel::WriteMode::solid_only, 0);
				break;
		}
	};

	// A region only needs the pieces reaching into it
	auto reaches = [&](const Stamp& stamp) { return stamp.grid != nullptr && (full || intersects(stamp.bounds(), covered)); };

	if (request.base != nullptr) {
		voxel::LatticePlacement at_zero {.offset = -origin};
		result += voxel::stampVolume(target, *request.base, at_zero, voxel::identityRemap(), voxel::WriteMode::replace, 0);
		uint8_t id = claim(0);
		result +=
		    voxel::stampVolume(owner, *request.base, at_zero, claim_table(voxel::identityRemap(), id), voxel::WriteMode::replace, 0);
	}
	for (const Stamp& stamp : request.stamps) {
		if (reaches(stamp)) {
			write(stamp);
		}
	}

	// changedRegion never asks for a region with buckets
	if (full) {
		for (const auto& [seed, id] : request.buckets) {
			result += voxel::floodFill(target, seed - origin, id);
		}
	}

	if (request.preview.has_value() && reaches(*request.preview)) {
		write(*request.preview);
	}

	if (out.has_projections) {
		projectShape(target, origin, request.palette, out.projections);
	}

	bool split_exhausted = false;
	out.parts = splitByOwner<Part>(target, owner, origin, owner_keys, split_exhausted);
	out.pool_exhausted = result.pool_exhausted || split_exhausted;
	return out;
}

auto changedRegion(const Request& before, const Request& after) -> std::optional<voxel::EditBounds> {
	if (!before.buckets.empty() || !after.buckets.empty() || after.projections) {
		return std::nullopt;
	}
	if (before.base_source != after.base_source || (before.base == nullptr) != (after.base == nullptr)) {
		return std::nullopt;
	}

	std::unordered_map<uint64_t, const Stamp*> old_by_id;
	old_by_id.reserve(before.stamps.size());
	for (const Stamp& stamp : before.stamps) {
		if (!old_by_id.emplace(stamp.id, &stamp).second) {
			return std::nullopt;
		}
	}

	voxel::EditBounds changed {};
	std::unordered_map<uint64_t, bool> in_both;
	std::vector<uint64_t> kept_after;
	in_both.reserve(after.stamps.size());
	for (const Stamp& stamp : after.stamps) {
		auto found = old_by_id.find(stamp.id);
		if (found == old_by_id.end()) {
			changed = voxel::boundsUnion(changed, stamp.bounds());
			continue;
		}
		bool same = stamp.sameAs(*found->second);
		if (!in_both.emplace(stamp.id, same).second) {
			return std::nullopt;
		}
		if (same) {
			kept_after.push_back(stamp.id);
		} else {
			changed = voxel::boundsUnion(changed, voxel::boundsUnion(found->second->bounds(), stamp.bounds()));
		}
	}
	std::vector<uint64_t> kept_before;
	for (const Stamp& stamp : before.stamps) {
		auto found = in_both.find(stamp.id);
		if (found == in_both.end()) {
			changed = voxel::boundsUnion(changed, stamp.bounds());
		} else if (found->second) {
			kept_before.push_back(stamp.id);
		}
	}
	if (kept_before != kept_after) {
		return std::nullopt;
	}

	bool same_preview = before.preview.has_value() == after.preview.has_value() &&
	                    (!after.preview.has_value() || after.preview->sameAs(*before.preview));
	if (!same_preview) {
		if (before.preview.has_value()) {
			changed = voxel::boundsUnion(changed, before.preview->bounds());
		}
		if (after.preview.has_value()) {
			changed = voxel::boundsUnion(changed, after.preview->bounds());
		}
	}
	if (changed.empty()) {
		return voxel::EditBounds {};
	}

	glm::ivec3 first = brickFloor(changed.min);
	glm::ivec3 last = brickFloor(changed.max);
	voxel::EditBounds region {first * k_dim, ((last + 1) * k_dim) - 1};

	// Rebuilding most of the shape costs the same as all of it
	voxel::EditBounds whole =
	    after.base != nullptr ? voxel::EditBounds {glm::ivec3(0), glm::ivec3(after.base->voxelDims()) - 1} : voxel::EditBounds {};
	for (const Stamp& stamp : after.stamps) {
		if (stamp.kind == PieceKind::fill) {
			whole = voxel::boundsUnion(whole, stamp.bounds());
		}
	}
	auto bricks = [](const voxel::EditBounds& box) -> int64_t {
		if (box.empty()) {
			return 0;
		}
		glm::ivec3 dims = brickFloor(box.max) - brickFloor(box.min) + 1;
		return static_cast<int64_t>(dims.x) * dims.y * dims.z;
	};
	if (bricks(region) * 2 > bricks(whole)) {
		return std::nullopt;
	}
	return region;
}

auto applyFull(std::vector<VoxelPart>& parts, Result& result, bool slack, std::vector<std::unique_ptr<voxel::Volume>>& retired)
    -> ApplyStats {
	ApplyStats stats;
	for (size_t i = parts.size(); i-- > 0;) {
		VoxelPart& part = parts[i];
		if (std::ranges::any_of(result.parts, [&](const Part& incoming) { return incoming.key == part.key; })) {
			continue;
		}
		retirePart(part, retired);
		parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
		stats.parts_changed = true;
	}

	for (Part& incoming : result.parts) {
		auto found = std::ranges::find(parts, incoming.key, &VoxelPart::key);
		if (found == parts.end()) {
			parts.emplace_back();
			parts.back().key = incoming.key;
			found = parts.end() - 1;
			stats.parts_changed = true;
		}
		VoxelPart& part = *found;

		const voxel::Volume& source = *incoming.volume;
		glm::ivec3 wanted_min = incoming.origin;
		glm::ivec3 wanted_max = incoming.origin + glm::ivec3(source.voxelDims()) - 1;
		bool fits = part.volume != nullptr && glm::all(glm::greaterThanEqual(wanted_min, part.origin)) &&
		            glm::all(glm::lessThan(wanted_max, part.origin + glm::ivec3(part.volume->voxelDims())));
		if (!fits) {
			// Growing makes a new volume
			glm::ivec3 room = slackAround(wanted_min, wanted_max, slack);
			++stats.grown;
			bool grown = growPart(part, {wanted_min - room, wanted_max + room});
			if (!grown) {
				grown = growPart(part, {wanted_min, wanted_max});
			}
			if (!grown && part.volume == nullptr) {
				++stats.left_out;
				parts.erase(found);
				continue;
			}
			if (!grown) {
				++stats.clipped;
			}
		}

		const auto [part_first, part_last] = partBricks(part);
		if (copyBricks(part, &incoming, part_first, part_last)) {
			++part.revision;
			part.split_pending = true;
			++stats.changed;
		}
	}
	// bricks go back to the pool
	result.parts.clear();
	return stats;
}

auto applyRegion(std::vector<VoxelPart>& parts, Result& result, bool slack, std::vector<std::unique_ptr<voxel::Volume>>& retired)
    -> ApplyStats {
	ApplyStats stats;
	if (!result.region.has_value() || result.region->empty()) {
		result.parts.clear();
		return stats;
	}
	glm::ivec3 region_first = brickFloor(result.region->min);
	glm::ivec3 region_last = brickFloor(result.region->max);

	auto incoming_of = [&](uint64_t key) -> const Part* {
		const auto found = std::ranges::find(result.parts, key, &Part::key);
		return found != result.parts.end() ? &*found : nullptr;
	};

	std::vector<bool> touched(parts.size(), false);
	for (size_t i = 0; i < parts.size(); ++i) {
		VoxelPart& part = parts[i];
		if (part.volume == nullptr) {
			continue;
		}
		const Part* incoming = incoming_of(part.key);
		auto [part_first, part_last] = partBricks(part);
		bool overlaps =
		    glm::all(glm::lessThanEqual(part_first, region_last)) && glm::all(glm::lessThanEqual(region_first, part_last));
		if (!overlaps && incoming == nullptr) {
			continue;
		}

		if (incoming != nullptr) {
			glm::ivec3 wanted_min = incoming->origin;
			glm::ivec3 wanted_max = incoming->origin + glm::ivec3(incoming->volume->voxelDims()) - 1;
			bool fits = glm::all(glm::greaterThanEqual(wanted_min, part.origin)) &&
			            glm::all(glm::lessThan(wanted_max, part.origin + glm::ivec3(part.volume->voxelDims())));
			if (!fits) {
				++stats.grown;
				if (!growKeeping(part, {wanted_min, wanted_max}, slack)) {
					++stats.clipped;
				}
				std::tie(part_first, part_last) = partBricks(part);
			}
		}

		glm::ivec3 first = glm::max(part_first, region_first);
		glm::ivec3 last = glm::min(part_last, region_last);
		if (glm::all(glm::lessThanEqual(first, last)) && copyBricks(part, incoming, first, last)) {
			++part.revision;
			part.split_pending = true;
			++stats.changed;
			touched[i] = true;
		}
	}

	for (Part& incoming : result.parts) {
		if (std::ranges::contains(parts, incoming.key, &VoxelPart::key)) {
			continue;
		}
		VoxelPart part;
		part.key = incoming.key;
		glm::ivec3 wanted_min = incoming.origin;
		glm::ivec3 wanted_max = incoming.origin + glm::ivec3(incoming.volume->voxelDims()) - 1;
		glm::ivec3 room = slackAround(wanted_min, wanted_max, slack);
		++stats.grown;
		if (!growPart(part, {wanted_min - room, wanted_max + room}) && !growPart(part, {wanted_min, wanted_max})) {
			++stats.left_out;
			continue;
		}
		auto [part_first, part_last] = partBricks(part);
		copyBricks(part, &incoming, part_first, part_last);
		part.split_pending = true;
		++stats.changed;
		parts.push_back(std::move(part));
		touched.push_back(false);
		stats.parts_changed = true;
	}

	for (size_t i = touched.size(); i-- > 0;) {
		if (!touched[i] || parts[i].volume == nullptr || parts[i].volume->solidVoxelCount() != 0) {
			continue;
		}
		retirePart(parts[i], retired);
		parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
		stats.parts_changed = true;
	}

	result.parts.clear();
	return stats;
}

}
