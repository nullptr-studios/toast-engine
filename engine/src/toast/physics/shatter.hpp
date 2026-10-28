/**
 * @file shatter.hpp
 * @author Xein
 * @date 17 Oct 2026
 * @brief Voronoi cracks
 */

#pragma once

#include "fracture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <span>
#include <unordered_set>
#include <vector>

namespace physics {

struct ShatterPlan {
	std::vector<glm::ivec3> crack;
	float band_outer = 0.0f;
	uint32_t cells = 0;
	const char* stop = "ok";    // debug
	float width = 0.0f;
	float density = 0.0f;
	size_t band_voxels = 0;
};

inline constexpr float k_min_crack_half_width = 0.5f * 1.7320508f;

[[nodiscard]]
constexpr auto patternShatters(FracturePattern pattern) -> bool {
	return pattern != FracturePattern::ductile;
}

[[nodiscard]]
inline auto crackThickness() -> float {
	return 2.0f * k_min_crack_half_width * voxel::k_voxel_size;
}

[[nodiscard]]
inline auto minChunkVoxels(float crack_thickness) -> uint32_t {
	const float side = (2.0f * crack_thickness) / voxel::k_voxel_size;
	return static_cast<uint32_t>(std::ceil(side * side * side));
}

[[nodiscard]]
inline auto craterVolume(const Crater& crater) -> float {
	return capsuleVolume(crater.radius, glm::length(crater.end - crater.start));
}

[[nodiscard]]
inline auto planShatter(
    const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials, const Crater& crater,
    Hardness max_hardness
) -> ShatterPlan {
	ZoneScopedN("physics::PlanShatter");
	ShatterPlan out;
	out.band_outer = craterMaxRadius(crater);

	const FractureValues& values = crater.profile;
	if (not crater.breaks || not(crater.shatter_energy > 0.0f) || not patternShatters(crater.pattern) ||
	    not(crater.radius > 0.0f) || materials.materials.empty()) {
		out.stop = "preconditions";
		return out;
	}

	const float crater_volume = craterVolume(crater);
	out.density = crater.shatter_energy / crater_volume;
	if (out.density < values.shatter_threshold * crater.toughness) {
		out.stop = "energy density below the threshold of the material";
		return out;
	}

	const float threshold = crackThickness();
	const float min_width = 2.0f * threshold;
	const float max_width = std::max(tunables().fracture_max_shatter_distance, min_width);

	const float cell_size = std::max(values.shatter_cell_size, threshold);
	const float length = glm::length(crater.end - crater.start);
	const float base_volume = capsuleVolume(crater.radius, length);
	const float fill = std::min(1.0f, 3.0f * threshold / cell_size);
	const auto band_volume_of = [&](float w) { return capsuleVolume(crater.radius + w, length) - base_volume; };
	const auto width_for = [&](float mean_toughness) {
		// widest band for the shatter the enerby pays for
		const auto cost_of = [&](float w) { return mean_toughness * values.shatter_crack_cost * fill * band_volume_of(w); };
		if (cost_of(max_width) <= crater.shatter_energy) {
			return max_width;
		}
		if (not(cost_of(min_width) < crater.shatter_energy)) {
			return min_width;
		}
		float low = min_width;
		float high = max_width;
		for (int i = 0; i < 24; ++i) {
			const float middle = 0.5f * (low + high);
			(cost_of(middle) <= crater.shatter_energy ? low : high) = middle;
		}
		return low;
	};

	// everything that could crack
	struct BandVoxel {
		glm::ivec3 voxel;
		glm::vec3 center;
		float outside;
	};

	std::vector<BandVoxel> band;
	float toughness_sum = 0.0f;
	const float edge_min = craterMinRadius(crater);
	const float edge_max = craterMaxRadius(crater);
	const auto collect = [&](float limit) {
		ZoneScopedN("physics::PlanShatter::Band");
		band.clear();
		toughness_sum = 0.0f;
		const float reach = edge_max + limit;
		forEachSolidVoxel(
		    volume,
		    glm::min(crater.start, crater.end) - glm::vec3(reach),
		    glm::max(crater.start, crater.end) + glm::vec3(reach),
		    [&](glm::ivec3 v, glm::vec3 center, uint8_t index) {
			    const glm::vec3 axis_point = closestPointOnSegment(center, crater.start, crater.end);
			    const float axis_distance = glm::length(center - axis_point);
			    if (axis_distance <= edge_min || axis_distance - edge_max > limit) {
				    return;
			    }
			    const float outside = axis_distance - craterRadiusAt(crater, center);
			    if (outside <= 0.0f || outside > limit) {
				    return;
			    }
			    const voxel::PhysicalMaterial& material = materials.materials[voxel::resolveMaterialIndex(palette, materials, index)];
			    if (not material.breakableBy(max_hardness) || not damageReaches(crater.reachable, volume, v, axis_point, center)) {
				    return;
			    }
			    band.push_back({.voxel = v, .center = center, .outside = outside});
			    toughness_sum += material.toughness;
		    }
		);
	};

	// guess the width from what was hit
	float width = width_for(crater.toughness);
	collect(width);
	if (band.empty()) {
		out.stop = "no breakable material around the crater";
		return out;
	}
	const float corrected = width_for(toughness_sum / static_cast<float>(band.size()));
	if (corrected > width) {
		collect(corrected);
	} else {
		std::erase_if(band, [corrected](const BandVoxel& b) { return b.outside > corrected; });
	}
	width = corrected;
	out.width = width;
	out.band_outer = edge_max + width;
	out.band_voxels = band.size();
	if (band.size() < 2) {
		out.stop = "no breakable material around the crater";
		return out;
	}

	const float cells_wanted = std::clamp(
	    band_volume_of(width) / (cell_size * cell_size * cell_size),
	    2.0f,
	    static_cast<float>(std::max(tunables().fracture_max_shatter_cells, 2u))
	);
	const auto cell_count = static_cast<uint32_t>(cells_wanted);

	// seeds
	Rng rng {streamSeed(crater.seed, FractureStream::shatter_points)};
	const float min_gap = 2.0f * threshold;
	const float min_gap_squared = min_gap * min_gap;
	std::vector<glm::vec3> seeds;
	for (uint32_t attempt = 0; attempt < cell_count * 12 && seeds.size() < cell_count; ++attempt) {
		const glm::vec3 point = band[rng.below(static_cast<uint32_t>(band.size()))].center;
		const bool crowded = std::ranges::any_of(seeds, [&](const glm::vec3& s) {
			const glm::vec3 offset = s - point;
			return glm::dot(offset, offset) < min_gap_squared;
		});
		if (not crowded) {
			seeds.push_back(point);
		}
	}
	if (seeds.size() < 2) {
		out.stop = "fewer than 2 seeds";
		return out;
	}
	out.cells = static_cast<uint32_t>(seeds.size());

	// fibrous
	const bool fibrous = crater.pattern == FracturePattern::fibrous;
	const glm::uvec3 dims = volume.voxelDims();
	glm::vec3 grain {0.0f};
	if (dims.x >= dims.y && dims.x >= dims.z) {
		grain[0] = 1.0f;
	} else {
		grain[(dims.y >= dims.z ? 1 : 2)] = 1.0f;
	}
	const float stretch = std::max(values.shatter_stretch, 1.0f);
	const auto metric_squared = [&](const glm::vec3& offset) {
		if (not fibrous) {
			return glm::dot(offset, offset);
		}
		const float along = glm::dot(offset, grain);
		const glm::vec3 lateral = offset - (grain * along);
		const float stretched = along / stretch;
		return glm::dot(lateral, lateral) + (stretched * stretched);
	};

	glm::ivec3 band_low {std::numeric_limits<int32_t>::max()};
	glm::ivec3 band_high {std::numeric_limits<int32_t>::min()};
	for (const BandVoxel& b : band) {
		band_low = glm::min(band_low, b.voxel);
		band_high = glm::max(band_high, b.voxel);
	}
	const glm::ivec3 band_dims = band_high - band_low + 1;
	const auto band_index = [&](const glm::ivec3& v) {
		const glm::ivec3 i = v - band_low;
		return (((static_cast<size_t>(i.z) * static_cast<size_t>(band_dims.y)) + static_cast<size_t>(i.y)) *
		        static_cast<size_t>(band_dims.x)) +
		       static_cast<size_t>(i.x);
	};
	std::vector<uint8_t> band_mask(
	    static_cast<size_t>(band_dims.x) * static_cast<size_t>(band_dims.y) * static_cast<size_t>(band_dims.z), 0
	);
	for (const BandVoxel& b : band) {
		band_mask[band_index(b.voxel)] = 1;
	}
	const auto in_band = [&](const glm::ivec3& v) {
		return glm::all(glm::greaterThanEqual(v, band_low)) && glm::all(glm::lessThanEqual(v, band_high)) &&
		       band_mask[band_index(v)] != 0;
	};

	// solid material
	const glm::ivec3 dims_i = glm::ivec3(dims);
	const std::array<glm::ivec3, k_unglue_neighbor_count> k_faces {
	  glm::ivec3 { 1,  0,  0},
	  glm::ivec3 {-1,  0,  0},
	  glm::ivec3 { 0,  1,  0},
	  glm::ivec3 { 0, -1,  0},
	  glm::ivec3 { 0,  0,  1},
	  glm::ivec3 { 0,  0, -1},
	};
	const auto touches_wall = [&](const glm::ivec3& v) {
		for (const glm::ivec3& offset : k_faces) {
			const glm::ivec3 n = v + offset;
			if (glm::any(glm::lessThan(n, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(n, dims_i)) ||
			    volume.materialAt(n) == voxel::k_empty_palette_index || in_band(n)) {
				continue;
			}
			const glm::vec3 center = (glm::vec3 {n} + 0.5f) * voxel::k_voxel_size;
			const float radius = craterRadiusAt(crater, center);
			if (pointSegmentDistanceSquared(center, crater.start, crater.end) > radius * radius) {
				return true;
			}
		}
		return false;
	};

	struct Owner {
		size_t nearest = 0;
		size_t second = 0;
		float gap = 0.0f;    // d2 - d1
	};

	std::vector<Owner> owners(band.size());
	std::vector<uint32_t> cell_voxels(seeds.size(), 0);
	for (size_t b = 0; b < band.size(); ++b) {
		float d1 = std::numeric_limits<float>::max();
		float d2 = std::numeric_limits<float>::max();
		Owner& owner = owners[b];
		for (size_t i = 0; i < seeds.size(); ++i) {
			const float d = metric_squared(band[b].center - seeds[i]);
			if (d < d1) {
				d2 = d1;
				owner.second = owner.nearest;
				d1 = d;
				owner.nearest = i;
			} else if (d < d2) {
				d2 = d;
				owner.second = i;
			}
		}
		owner.gap = std::sqrt(d2) - std::sqrt(d1);
		++cell_voxels[owner.nearest];
	}

	const uint32_t min_cell_voxels = minChunkVoxels(threshold);
	const float release = std::clamp(values.shatter_release, 0.0f, 1.0f);
	std::vector<bool> released(seeds.size(), false);
	std::vector<size_t> eligible;
	for (size_t i = 0; i < seeds.size(); ++i) {
		if (cell_voxels[i] >= min_cell_voxels) {
			eligible.push_back(i);
			released[i] = rng.chance(release);
		}
	}
	if (eligible.empty()) {
		out.stop = "cells too small to come off";
		return out;
	}
	if (std::ranges::none_of(eligible, [&](size_t i) { return released[i]; })) {
		released[eligible[rng.below(static_cast<uint32_t>(eligible.size()))]] = true;
	}

	const uint64_t crumble_seed = streamSeed(crater.seed, FractureStream::granular);
	for (size_t b = 0; b < band.size(); ++b) {
		const Owner& owner = owners[b];
		const bool mine = released[owner.nearest];
		const bool border = owner.gap < threshold && (mine || released[owner.second]);
		bool crack = border;
		crack = crack || (mine && touches_wall(band[b].voxel));
		crack = crack ||
		        (mine && crater.pattern == FracturePattern::granular && hash01(crumble_seed, band[b].voxel) < values.shatter_crumble);
		if (crack) {
			out.crack.push_back(band[b].voxel);
		}
	}
	return out;
}

struct CoreVoxel {
	glm::ivec3 voxel {0};
	glm::vec3 center {0.0f};
};

struct CoreSplit {
	std::vector<glm::ivec3> crack;
	uint32_t parts = 0;
	float density = 0.0f;
};

[[nodiscard]]
inline auto planCoreSplit(std::span<const CoreVoxel> core, const Crater& crater) -> CoreSplit {
	ZoneScopedN("physics::PlanCoreSplit");
	CoreSplit out;
	const FractureValues& values = crater.profile;
	if (not(crater.fragment_energy > 0.0f) || not(crater.radius > 0.0f)) {
		return out;
	}
	out.density = crater.fragment_energy / craterVolume(crater);
	const float part_density = std::max(values.fragment_threshold * crater.toughness, 1.0e-9f);
	if (out.density < part_density) {
		return out;
	}

	Rng rng {streamSeed(crater.seed, FractureStream::core_split)};
	const float doublings = std::log2(out.density / part_density);
	const float parts_wanted =
	    std::clamp(2.0f + std::floor(doublings), 2.0f, static_cast<float>(std::max(values.fragment_max_parts, 2u)));

	const float threshold = crackThickness();
	const auto room = static_cast<uint32_t>(core.size() / minChunkVoxels(threshold));
	const uint32_t parts = std::min(static_cast<uint32_t>(parts_wanted), room);
	if (parts < 2) {
		return out;
	}

	std::vector<glm::vec3> seeds {core[rng.below(static_cast<uint32_t>(core.size()))].center};
	while (seeds.size() < parts) {
		glm::vec3 best = seeds.front();
		float best_gap = -1.0f;
		for (int attempt = 0; attempt < 12; ++attempt) {
			const glm::vec3 point = core[rng.below(static_cast<uint32_t>(core.size()))].center;
			float gap = std::numeric_limits<float>::max();
			for (const glm::vec3& s : seeds) {
				gap = std::min(gap, glm::distance(s, point));
			}
			if (gap > best_gap) {
				best_gap = gap;
				best = point;
			}
		}
		if (best_gap < 2.0f * threshold) {
			break;    // too small
		}
		seeds.push_back(best);
	}
	if (seeds.size() < 2) {
		return out;
	}
	out.parts = static_cast<uint32_t>(seeds.size());

	for (const CoreVoxel& c : core) {
		float d1 = std::numeric_limits<float>::max();
		float d2 = std::numeric_limits<float>::max();
		for (const glm::vec3& seed_point : seeds) {
			const float d = glm::distance(c.center, seed_point);
			if (d < d1) {
				d2 = d1;
				d1 = d;
			} else if (d < d2) {
				d2 = d;
			}
		}
		if (d2 - d1 < threshold) {
			out.crack.push_back(c.voxel);
		}
	}
	return out;
}

}
