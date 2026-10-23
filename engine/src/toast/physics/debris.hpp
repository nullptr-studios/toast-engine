/**
 * @file debris.hpp
 * @author Xein
 * @date 17 Oct 2026
 * @brief Small voxel debris clusters
 */

#pragma once

#include "fracture_random.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <span>
#include <toast/export.hpp>
#include <vector>

namespace physics {

struct DebrisVoxel {
	glm::ivec3 voxel {0};
	uint8_t material = 0;
};

struct DebrisCluster {
	glm::ivec3 origin {0};
	uint8_t mask = 0;
	uint8_t material = 0;
	bool whole = false;
	glm::vec3 centre {0.0f};
	float scale = 1.0f;
	uint32_t voxel_count = 0;
};

struct DebrisSpawn {
	glm::vec3 position {};
	glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 velocity {};
	glm::vec3 angular_velocity {};
	uint8_t mask = 0;
	uint8_t material = 0;
	uint32_t albedo_roughness = 0;
	uint8_t metallic = 0;
	uint8_t reflectivity = 0;
	bool glass = false;
	uint8_t alpha = 255;
	/// Palette ior byte and 0 for the default
	uint8_t ior = 0;
	float scale = 1.0f;
};

struct DebrisBlast {
	glm::vec3 start {};
	glm::vec3 end {};
	float energy = 0.0f;
	float max_speed = 0.0f;
};

inline constexpr float k_max_debris_speed = 40.0f;
inline constexpr float k_debris_cone_spread = 0.5f;
inline constexpr float k_debris_max_spin = 8.0f;
inline constexpr float k_debris_min_scale = 0.55f;

[[nodiscard]]
constexpr auto debrisBit(const glm::ivec3& local) -> uint32_t {
	return static_cast<uint32_t>(local.x + (2 * local.y) + (4 * local.z));
}

[[nodiscard]]
inline auto wholeDebris(std::span<const DebrisVoxel> voxels) -> DebrisCluster {
	DebrisCluster out;
	out.whole = true;
	glm::ivec3 low = voxels.front().voxel;
	glm::ivec3 high = low;
	std::array<uint32_t, 256> counts {};
	for (const DebrisVoxel& v : voxels) {
		low = glm::min(low, v.voxel);
		high = glm::max(high, v.voxel);
		++counts[v.material];
	}

	const glm::vec3 extent = glm::vec3(high - low + 1);
	out.centre = (glm::vec3(low) + glm::vec3(high + 1)) * 0.5f;
	out.scale = std::max({extent.x, extent.y, extent.z}) * 0.5f;
	out.voxel_count = static_cast<uint32_t>(voxels.size());
	out.material = static_cast<uint8_t>(std::ranges::max_element(counts) - counts.begin());

	for (const DebrisVoxel& v : voxels) {
		const glm::vec3 point = glm::vec3(v.voxel) + 0.5f;
		const uint32_t bit =
		    (point.x >= out.centre.x ? 1u : 0u) | (point.y >= out.centre.y ? 2u : 0u) | (point.z >= out.centre.z ? 4u : 0u);
		out.mask |= static_cast<uint8_t>(1u << bit);
	}
	return out;
}

[[nodiscard]]
inline auto clusterDebris(std::span<const DebrisVoxel> voxels, const glm::ivec3& jitter) -> std::vector<DebrisCluster> {
	struct Entry {
		glm::ivec3 cell;
		uint32_t bit;
		uint8_t material;
	};

	std::vector<Entry> entries;
	entries.reserve(voxels.size());
	for (const DebrisVoxel& v : voxels) {
		const glm::ivec3 shifted = v.voxel - jitter;
		const glm::ivec3 cell {shifted.x >> 1, shifted.y >> 1, shifted.z >> 1};
		entries.push_back({.cell = cell, .bit = debrisBit(shifted - (cell * 2)), .material = v.material});
	}
	std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
		if (a.cell.z != b.cell.z) {
			return a.cell.z < b.cell.z;
		}
		if (a.cell.y != b.cell.y) {
			return a.cell.y < b.cell.y;
		}
		if (a.cell.x != b.cell.x) {
			return a.cell.x < b.cell.x;
		}
		return a.bit < b.bit;
	});

	std::vector<DebrisCluster> out;
	for (size_t first = 0; first < entries.size();) {
		size_t last = first;
		std::array<uint8_t, 8> material {};
		uint32_t present = 0;
		while (last < entries.size() && entries[last].cell == entries[first].cell) {
			present |= 1u << entries[last].bit;
			material[entries[last].bit] = entries[last].material;
			++last;
		}

		uint8_t best = 0;
		int best_count = 0;
		for (uint32_t bit = 0; bit < 8; ++bit) {
			if ((present & (1u << bit)) == 0) {
				continue;
			}
			int same = 0;
			for (uint32_t other = 0; other < 8; ++other) {
				same += ((present & (1u << other)) != 0 && material[other] == material[bit]) ? 1 : 0;
			}
			if (same > best_count) {
				best_count = same;
				best = material[bit];
			}
		}
		out.push_back(
		    DebrisCluster {
		      .origin = (entries[first].cell * 2) + jitter,
		      .mask = static_cast<uint8_t>(present),
		      .material = best,
		    }
		);
		first = last;
	}
	return out;
}

[[nodiscard]]
inline auto debrisSpeed(float energy, float mass, float scale) -> float {
	if (not(energy > 0.0f) || not(mass > 0.0f) || not(scale > 0.0f)) {
		return 0.0f;
	}
	return std::min(std::sqrt(2.0f * energy / mass) * scale, k_max_debris_speed);
}

[[nodiscard]]
inline auto debrisConeDirection(const glm::vec3& axis, Rng& rng, float spread) -> glm::vec3 {
	const glm::vec3 tilted = axis + (rng.direction() * spread);
	const float length = glm::length(tilted);
	return length > 1.0e-4f ? tilted / length : axis;
}

TOAST_API void stepDebris(size_t steps);

TOAST_API void clearDebris();

}
