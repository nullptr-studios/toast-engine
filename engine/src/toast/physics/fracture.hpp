/**
 * @file fracture.hpp
 * @author Xein
 * @date 12 Oct 2026
 * @brief How much of a voxel volume a hit breaks
 */

#pragma once

#include "hardness.hpp"
#include "physics_settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <toast/voxel/voxel_volume.hpp>
#include <unordered_set>
#include <vector>

namespace physics {

inline constexpr float k_voxel_volume = voxel::k_voxel_size * voxel::k_voxel_size * voxel::k_voxel_size;

inline constexpr float k_joules_per_kilojoule = 1000.0f;    // we use kJ to not have values in the millions

[[nodiscard]]
constexpr auto voxelBreakEnergy(const voxel::PhysicalMaterial& material) -> float {
	return material.toughness * k_voxel_volume;
}

struct FractureRequest {
	glm::vec3 point {};
	glm::vec3 direction {};
	float radius = 0.0f;
	float energy_density = 0.0f;
	Hardness max_hardness = k_hardest;
	float depth_share = tunables().fracture_depth_share;
	float max_depth = tunables().fracture_max_depth;
	float max_overshoot = tunables().fracture_max_overshoot;
	float bond_cost = tunables().fracture_bond_cost;
	uint32_t max_unglue_voxels = tunables().fracture_max_unglue_voxels;
};

inline constexpr uint32_t k_unglue_neighbor_count = 6;

[[nodiscard]]
constexpr auto voxelKey(const glm::ivec3& v) -> uint64_t {
	return (static_cast<uint64_t>(v.x) << 42) | (static_cast<uint64_t>(v.y) << 21) | static_cast<uint64_t>(v.z);
}

struct UnglueRegion {
	std::unordered_set<uint64_t> keys;
	glm::vec3 low {std::numeric_limits<float>::max()};
	glm::vec3 high {std::numeric_limits<float>::lowest()};
	float cost = 0.0f;

	[[nodiscard]]
	auto empty() const -> bool {
		return keys.empty();
	}
};

struct Crater {
	glm::vec3 start {};
	glm::vec3 end {};
	float radius = 0.0f;
	bool breaks = false;
	UnglueRegion unglue;
	std::unordered_set<uint64_t> reachable;
};

struct FractureCandidate {
	float depth = 0.0f;
	float distance = 0.0f;
	float cost = 0.0f;
	bool breakable = false;
	bool on_axis = false;
};

struct Penetration {
	float depth = 0.0f;
	float spent = 0.0f;
	uint32_t voxels = 0;
};

template<typename Visit>
void forEachSolidVoxel(const voxel::Volume& volume, const glm::vec3& low, const glm::vec3& high, Visit&& visit) {
	const glm::ivec3 voxel_dims = glm::ivec3(volume.voxelDims());
	glm::ivec3 first = glm::ivec3(glm::floor(low / voxel::k_voxel_size));
	glm::ivec3 last = glm::ivec3(glm::ceil(high / voxel::k_voxel_size)) - 1;
	first = glm::clamp(first, glm::ivec3(0), voxel_dims - 1);
	last = glm::clamp(last, glm::ivec3(0), voxel_dims - 1);
	if (first.x > last.x || first.y > last.y || first.z > last.z) {
		return;
	}

	// yeah this is O(n^6) 🥀
	const auto brick_dim = static_cast<int32_t>(voxel::k_brick_dim);
	const glm::ivec3 first_brick = first / brick_dim;
	const glm::ivec3 last_brick = last / brick_dim;
	for (int32_t bz = first_brick.z; bz <= last_brick.z; ++bz) {
		for (int32_t by = first_brick.y; by <= last_brick.y; ++by) {
			for (int32_t bx = first_brick.x; bx <= last_brick.x; ++bx) {
				const glm::ivec3 brick {bx, by, bz};
				if (volume.entryAt(brick).tag() == voxel::BrickTag::empty) {
					continue;
				}
				const glm::ivec3 brick_first = glm::max(first, brick * brick_dim);
				const glm::ivec3 brick_last = glm::min(last, (brick * brick_dim) + (brick_dim - 1));
				for (int32_t z = brick_first.z; z <= brick_last.z; ++z) {
					for (int32_t y = brick_first.y; y <= brick_last.y; ++y) {
						for (int32_t x = brick_first.x; x <= brick_last.x; ++x) {
							const glm::ivec3 v {x, y, z};
							const uint8_t palette_index = volume.materialAt(v);
							if (palette_index == voxel::k_empty_palette_index) {
								continue;
							}
							visit(v, (glm::vec3 {v} + 0.5f) * voxel::k_voxel_size, palette_index);
						}
					}
				}
			}
		}
	}
}

[[nodiscard]]
inline auto pointSegmentDistanceSquared(const glm::vec3& point, const glm::vec3& start, const glm::vec3& end) -> float {
	const glm::vec3 axis = end - start;
	const float axis_squared = glm::dot(axis, axis);
	const float along = axis_squared > 0.0f ? glm::clamp(glm::dot(point - start, axis) / axis_squared, 0.0f, 1.0f) : 0.0f;
	const glm::vec3 gap = point - (start + (axis * along));
	return glm::dot(gap, gap);
}

[[nodiscard]]
inline auto closestPointOnSegment(const glm::vec3& point, const glm::vec3& start, const glm::vec3& end) -> glm::vec3 {
	const glm::vec3 axis = end - start;
	const float axis_squared = glm::dot(axis, axis);
	const float along = axis_squared > 0.0f ? glm::clamp(glm::dot(point - start, axis) / axis_squared, 0.0f, 1.0f) : 0.0f;
	return start + (axis * along);
}

[[nodiscard]]
inline auto reachesThroughMaterial(const voxel::Volume& volume, const glm::vec3& from, const glm::vec3& to) -> bool {
	// damage travels through material
	const glm::ivec3 dims = glm::ivec3(volume.voxelDims());
	const float max_air = 1.5f * voxel::k_voxel_size;
	const glm::vec3 span = to - from;
	const float length = glm::length(span);
	const auto steps = static_cast<int32_t>(std::ceil(length / (0.5f * voxel::k_voxel_size)));
	if (steps <= 0) {
		return true;
	}

	const float step_length = length / static_cast<float>(steps);
	float air = 0.0f;
	for (int32_t i = 1; i < steps; ++i) {
		const glm::vec3 sample = from + (span * (static_cast<float>(i) / static_cast<float>(steps)));
		const glm::ivec3 v = glm::ivec3(glm::floor(sample / voxel::k_voxel_size));
		const bool solid = glm::all(glm::greaterThanEqual(v, glm::ivec3(0))) && glm::all(glm::lessThan(v, dims)) &&
		                   volume.materialAt(v) != voxel::k_empty_palette_index;
		if (not solid) {
			air += step_length;
			if (air > max_air) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]]
inline auto reachableMaterial(const voxel::Volume& volume, const glm::vec3& start, const glm::vec3& end, float radius)
    -> std::unordered_set<uint64_t> {
	std::unordered_set<uint64_t> out;

	// voxel closest to the hit
	std::optional<glm::ivec3> seed;
	float closest = radius * radius;
	forEachSolidVoxel(volume, start - glm::vec3(radius), start + glm::vec3(radius), [&](glm::ivec3 v, glm::vec3 center, uint8_t) {
		const glm::vec3 offset = center - start;
		const float distance_squared = glm::dot(offset, offset);
		if (distance_squared <= closest) {
			closest = distance_squared;
			seed = v;
		}
	});
	if (not seed.has_value()) {
		return out;
	}

	const glm::ivec3 dims = glm::ivec3(volume.voxelDims());
	std::vector<glm::ivec3> stack {*seed};
	out.insert(voxelKey(*seed));
	while (not stack.empty()) {
		const glm::ivec3 v = stack.back();
		stack.pop_back();
		for (int32_t z = -1; z <= 1; ++z) {
			for (int32_t y = -1; y <= 1; ++y) {
				for (int32_t x = -1; x <= 1; ++x) {
					const glm::ivec3 next = v + glm::ivec3 {x, y, z};
					if (next == v || glm::any(glm::lessThan(next, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(next, dims)) ||
					    volume.materialAt(next) == voxel::k_empty_palette_index) {
						continue;
					}
					const glm::vec3 center = (glm::vec3 {next} + 0.5f) * voxel::k_voxel_size;
					if (pointSegmentDistanceSquared(center, start, end) > radius * radius) {
						continue;
					}
					if (out.insert(voxelKey(next)).second) {
						stack.push_back(next);
					}
				}
			}
		}
	}
	return out;
}

[[nodiscard]]
inline auto damageReaches(
    const std::unordered_set<uint64_t>& reachable, const voxel::Volume& volume, const glm::ivec3& voxel, const glm::vec3& from,
    const glm::vec3& center
) -> bool {
	return reachable.contains(voxelKey(voxel)) || reachesThroughMaterial(volume, from, center);
}

[[nodiscard]]
constexpr auto hitBreaks(const voxel::PhysicalMaterial& material, float energy_density, Hardness max_hardness) -> bool {
	return material.breakableBy(max_hardness) && material.toughness <= energy_density;
}

[[nodiscard]]
inline auto hitMaterial(
    const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials, const glm::vec3& point,
    float reach
) -> std::optional<voxel::PhysicalMaterial> {
	std::optional<voxel::PhysicalMaterial> out;
	if (materials.materials.empty()) {
		return out;
	}
	reach = std::max(reach, voxel::k_voxel_size);
	float best = reach * reach;
	forEachSolidVoxel(volume, point - glm::vec3(reach), point + glm::vec3(reach), [&](glm::ivec3, glm::vec3 center, uint8_t index) {
		const glm::vec3 offset = center - point;
		const float distance_squared = glm::dot(offset, offset);
		if (distance_squared <= best) {
			best = distance_squared;
			out = materials.materials[voxel::resolveMaterialIndex(palette, materials, index)];
		}
	});
	return out;
}

[[nodiscard]]
inline auto fractureCandidates(
    const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials,
    const FractureRequest& request, const std::unordered_set<uint64_t>& reachable
) -> std::vector<FractureCandidate> {
	std::vector<FractureCandidate> out;
	if (materials.materials.empty() || not(request.radius > 0.0f)) {
		return out;
	}

	const float radius = request.radius;
	const bool digs = glm::dot(request.direction, request.direction) > 0.0f && request.max_depth > 0.0f;
	const glm::vec3 far = request.point + (digs ? request.direction * request.max_depth : glm::vec3 {0.0f});
	const glm::vec3 low = glm::min(request.point, far) - glm::vec3(radius);
	const glm::vec3 high = glm::max(request.point, far) + glm::vec3(radius);
	const float axis_reach = 0.5f * std::sqrt(3.0f) * voxel::k_voxel_size;    // half the diagonal of a voxel

	forEachSolidVoxel(volume, low, high, [&](glm::ivec3 voxel, glm::vec3 center, uint8_t index) {
		const glm::vec3 offset = center - request.point;
		const float distance = glm::length(offset);
		const voxel::PhysicalMaterial& material = materials.materials[voxel::resolveMaterialIndex(palette, materials, index)];
		FractureCandidate candidate {
		  .distance = distance,
		  .cost = voxelBreakEnergy(material),
		  .breakable = material.breakableBy(request.max_hardness),
		};

		if (distance <= radius) {
			if (damageReaches(reachable, volume, voxel, request.point, center)) {
				out.push_back(candidate);
			}
			return;
		}
		if (not digs) {
			return;
		}

		// capsule from p to p + dir * depth holds this voxel
		const float along = glm::dot(offset, request.direction);
		const glm::vec3 lateral_offset = offset - (request.direction * along);
		const float lateral_squared = glm::dot(lateral_offset, lateral_offset);
		if (along <= 0.0f || lateral_squared > radius * radius) {
			return;
		}
		candidate.depth = along - std::sqrt((radius * radius) - lateral_squared);
		if (candidate.depth > request.max_depth) {
			return;
		}
		candidate.on_axis = lateral_squared <= axis_reach * axis_reach;
		if (damageReaches(reachable, volume, voxel, request.point + (request.direction * along), center)) {
			out.push_back(candidate);
		}
	});
	return out;
}

[[nodiscard]]
inline auto penetrationDepth(std::span<const FractureCandidate> candidates, float energy, float max_depth) -> Penetration {
	Penetration out;
	// air between the hit and something else stops it
	const float max_gap = 1.5f * voxel::k_voxel_size;
	float reached = 0.0f;
	for (const FractureCandidate& candidate : candidates) {
		if (candidate.depth > max_depth || candidate.depth - reached > max_gap) {
			break;
		}
		reached = std::max(reached, candidate.depth);
		if (not candidate.breakable) {
			if (candidate.on_axis) {
				break;
			}
			continue;
		}
		if (out.spent + candidate.cost > energy) {
			break;
		}
		out.spent += candidate.cost;
		out.depth = candidate.depth;
		++out.voxels;
	}
	return out;
}

[[nodiscard]]
constexpr auto capsuleVolume(float radius, float length) -> float {
	return (std::numbers::pi_v<float> * radius * radius * length) +
	       ((4.0f / 3.0f) * std::numbers::pi_v<float> * radius * radius * radius);
}

[[nodiscard]]
inline auto overshootRadius(float energy_left, float toughness, float radius, float length, float max_overshoot) -> float {
	const float cap = std::max(max_overshoot, 0.0f) * radius;
	if (not(energy_left > 0.0f) || not(toughness > 0.0f) || not(cap > 0.0f)) {
		return 0.0f;
	}

	const float affordable = energy_left / toughness;
	const auto capsule_volume = [length](float r) { return capsuleVolume(r, length); };
	const float base = capsule_volume(radius);
	if (capsule_volume(radius + cap) - base <= affordable) {
		return cap;
	}

	float low = 0.0f;
	float high = cap;
	for (int i = 0; i < 24; ++i) {
		const float middle = 0.5f * (low + high);
		if (capsule_volume(radius + middle) - base <= affordable) {
			low = middle;
		} else {
			high = middle;
		}
	}
	return low;
}

[[nodiscard]]
inline auto planUnglue(
    const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials, const glm::vec3& start,
    const glm::vec3& end, float radius, Hardness max_hardness, float energy, float bond_cost, uint32_t max_voxels
) -> UnglueRegion;

[[nodiscard]]
inline auto planCrater(
    const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials,
    const FractureRequest& request
) -> Crater {
	Crater out {.start = request.point, .end = request.point};
	if (not std::isfinite(request.energy_density) || not(request.energy_density > 0.0f) || not(request.radius > 0.0f)) {
		return out;
	}

	// a hit never breaks anything of if the voxel is too tough or too hard
	const std::optional<voxel::PhysicalMaterial> hit = hitMaterial(volume, palette, materials, request.point, request.radius);
	if (not hit.has_value() || not hit->breakableBy(request.max_hardness)) {
		return out;
	}

	// as far as the crater can grow
	const bool digs = glm::dot(request.direction, request.direction) > 0.0f && request.max_depth > 0.0f;
	const glm::vec3 far = digs ? request.point + (request.direction * request.max_depth) : request.point;
	out.reachable = reachableMaterial(volume, request.point, far, request.radius * (1.0f + std::max(request.max_overshoot, 0.0f)));
	std::vector<FractureCandidate> candidates = fractureCandidates(volume, palette, materials, request, out.reachable);
	const auto deeper = std::ranges::partition(candidates, [](const FractureCandidate& c) { return c.depth <= 0.0f; });
	const std::span<FractureCandidate> inside(candidates.begin(), deeper.begin());
	const std::span<FractureCandidate> beyond(deeper.begin(), deeper.end());

	// the hit puts its energy density into all the material inside its fracture radius
	// we need to conserve energy so everything that doesn't get used gets propagated outwards
	const auto breakable = static_cast<float>(std::ranges::count_if(inside, &FractureCandidate::breakable));
	// a little slack so float sums never leave a hit exactly as tough as the material one voxel short
	const float budget = request.energy_density * breakable * k_voxel_volume * (1.0f + 1.0e-5f);

	// paid nearest first
	std::ranges::sort(inside, {}, &FractureCandidate::distance);
	float spent = 0.0f;
	float paid_cost = 0.0f;
	uint32_t paid = 0;
	float reached = 0.0f;
	bool short_of_energy = false;
	for (const FractureCandidate& candidate : inside) {
		if (not candidate.breakable) {
			continue;
		}
		if (spent + candidate.cost > budget) {
			short_of_energy = true;
			break;
		}
		spent += candidate.cost;
		paid_cost += candidate.cost;
		++paid;
		reached = candidate.distance;
	}
	if (paid == 0) {
		return out;    // not even the nearest voxel is affordable
	}
	out.breaks = true;
	if (short_of_energy) {
		out.radius = std::max(reached, 0.5f * voxel::k_voxel_size);
		return out;
	}
	out.radius = request.radius;

	float left = budget - spent;
	if (glm::dot(request.direction, request.direction) > 0.0f) {
		std::ranges::sort(beyond, {}, &FractureCandidate::depth);
		const Penetration dig = penetrationDepth(beyond, left * std::clamp(request.depth_share, 0.0f, 1.0f), request.max_depth);
		out.end = request.point + (request.direction * dig.depth);
		left -= dig.spent;
		paid_cost += dig.spent;
		paid += dig.voxels;
	}

	out.unglue = planUnglue(
	    volume,
	    palette,
	    materials,
	    out.start,
	    out.end,
	    request.radius,
	    request.max_hardness,
	    left,
	    request.bond_cost,
	    request.max_unglue_voxels
	);
	left -= out.unglue.cost;

	const float toughness = paid > 0 ? paid_cost / (static_cast<float>(paid) * k_voxel_volume) : hit->toughness;
	out.radius += overshootRadius(left, toughness, request.radius, glm::length(out.end - out.start), request.max_overshoot);
	return out;
}

[[nodiscard]]
inline auto planUnglue(
    const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials, const glm::vec3& start,
    const glm::vec3& end, float radius, Hardness max_hardness, float energy, float bond_cost, uint32_t max_voxels
) -> UnglueRegion {
	UnglueRegion out;
	if (not(energy > 0.0f) || not(bond_cost > 0.0f) || materials.materials.empty()) {
		return out;
	}

	const glm::ivec3 dims = glm::ivec3(volume.voxelDims());
	const auto inside = [&](const glm::ivec3& v) {
		return glm::all(glm::greaterThanEqual(v, glm::ivec3(0))) && glm::all(glm::lessThan(v, dims));
	};
	const auto material_at = [&](const glm::ivec3& v) -> const voxel::PhysicalMaterial* {
		if (not inside(v)) {
			return nullptr;
		}
		const uint8_t index = volume.materialAt(v);
		if (index == voxel::k_empty_palette_index) {
			return nullptr;
		}
		return &materials.materials[voxel::resolveMaterialIndex(palette, materials, index)];
	};

	const std::array<glm::ivec3, k_unglue_neighbor_count> k_faces {
	  glm::ivec3 { 1,  0,  0},
	  glm::ivec3 {-1,  0,  0},
	  glm::ivec3 { 0,  1,  0},
	  glm::ivec3 { 0, -1,  0},
	  glm::ivec3 { 0,  0,  1},
	  glm::ivec3 { 0,  0, -1}
	};

	// breakable voxels at the crater wall that touch something this hit cannot break
	std::vector<glm::ivec3> frontier;
	const float reach = radius + (1.5f * voxel::k_voxel_size);
	const glm::vec3 low = glm::min(start, end) - glm::vec3(reach);
	const glm::vec3 high = glm::max(start, end) + glm::vec3(reach);
	forEachSolidVoxel(volume, low, high, [&](glm::ivec3 v, glm::vec3 center, uint8_t) {
		if (pointSegmentDistanceSquared(center, start, end) > reach * reach) {
			return;
		}
		const voxel::PhysicalMaterial* self = material_at(v);
		if (self == nullptr || not self->breakableBy(max_hardness)) {
			return;
		}
		for (const glm::ivec3& face : k_faces) {
			const voxel::PhysicalMaterial* other = material_at(v + face);
			if (other != nullptr && not other->breakableBy(max_hardness)) {
				frontier.push_back(v);
				return;
			}
		}
	});
	if (frontier.empty()) {
		return out;
	}

	// everything breakable that hangs together with them
	std::vector<glm::ivec3> stack;
	const auto visit = [&](const glm::ivec3& v) {
		if (out.keys.insert(voxelKey(v)).second) {
			stack.push_back(v);
		}
	};
	for (const glm::ivec3& v : frontier) {
		visit(v);
	}

	float cost = 0.0f;
	while (not stack.empty()) {
		const glm::ivec3 v = stack.back();
		stack.pop_back();
		if (out.keys.size() > max_voxels) {
			return {};
		}

		const voxel::PhysicalMaterial* self = material_at(v);
		for (const glm::ivec3& face : k_faces) {
			const glm::ivec3 next = v + face;
			const voxel::PhysicalMaterial* other = material_at(next);
			if (other == nullptr) {
				continue;
			}
			if (other->breakableBy(max_hardness)) {
				visit(next);
			} else {
				cost += bond_cost * voxelBreakEnergy(*self);
			}
		}
		if (cost > energy) {
			return {};
		}
		out.low = glm::min(out.low, glm::vec3(v) * voxel::k_voxel_size);
		out.high = glm::max(out.high, (glm::vec3(v) + 1.0f) * voxel::k_voxel_size);
	}
	out.cost = cost;
	return out;
}

[[nodiscard]]
inline auto explosionDensityAt(float density, float distance, float falloff) -> float {
	return density / std::pow(std::max(distance, voxel::k_voxel_size), falloff);
}

[[nodiscard]]
inline auto explosionReach(float density, float falloff, float toughness, float max_radius) -> float {
	if (not(density > 0.0f) || not(falloff > 0.0f)) {
		return 0.0f;
	}
	if (not(toughness > 0.0f)) {
		return max_radius;
	}
	return std::min(std::pow(density / toughness, 1.0f / falloff), max_radius);
}

[[nodiscard]]
inline auto weakestToughness(const voxel::MaterialLibrary& materials, Hardness max_hardness) -> float {
	float out = std::numeric_limits<float>::infinity();
	for (const voxel::PhysicalMaterial& material : materials.materials) {
		if (material.breakableBy(max_hardness)) {
			out = std::min(out, material.toughness);
		}
	}
	return out;
}

[[nodiscard]]
inline auto impactDensity(float energy, float patch_radius) -> float {
	const float radius = std::max(patch_radius, voxel::k_voxel_size);
	return energy / (k_joules_per_kilojoule * (2.0f / 3.0f) * std::numbers::pi_v<float> * radius * radius * radius);
}

}
