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
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <span>
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
};

struct DebrisSpawn {
	glm::vec3 position {};
	glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 velocity {};
	glm::vec3 angular_velocity {};
	uint8_t mask = 0;
	uint8_t material = 0;
};

struct DebrisVolume {
	uint32_t record_index = 0;
	glm::mat4 world_to_voxel {1.0f};
	glm::vec3 aabb_min {};
	glm::vec3 aabb_max {};
};

struct DebrisBlast {
	glm::vec3 start {};
	glm::vec3 end {};
	float energy = 0.0f;
};

inline constexpr float k_max_debris_speed = 40.0f;
inline constexpr float k_debris_cone_spread = 0.5f;
inline constexpr float k_debris_max_spin = 8.0f;

[[nodiscard]]
constexpr auto debrisBit(const glm::ivec3& local) -> uint32_t {
	return static_cast<uint32_t>(local.x + (2 * local.y) + (4 * local.z));
}

[[nodiscard]]
inline auto clusterDebris(std::span<const DebrisVoxel> voxels, const glm::ivec3& jitter, Rng& rng, float split_chance)
    -> std::vector<DebrisCluster> {
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

		// a block sometimes breaks into 2 or 3 smaller pieces
		const auto count = static_cast<uint32_t>(std::popcount(present));
		uint32_t parts = 1;
		if (count > 1 && rng.chance(split_chance)) {
			parts = (count > 2 && rng.chance(0.5f)) ? 3u : 2u;
		}
		std::array<uint32_t, 3> part_masks {};
		for (uint32_t bit = 0; bit < 8; ++bit) {
			if ((present & (1u << bit)) != 0) {
				part_masks[parts == 1 ? 0 : rng.below(parts)] |= 1u << bit;
			}
		}

		for (uint32_t p = 0; p < parts; ++p) {
			if (part_masks[p] == 0) {
				continue;
			}
			// most common material of the part
			uint8_t best = 0;
			int best_count = 0;
			for (uint32_t bit = 0; bit < 8; ++bit) {
				if ((part_masks[p] & (1u << bit)) == 0) {
					continue;
				}
				int same = 0;
				for (uint32_t other = 0; other < 8; ++other) {
					same += ((part_masks[p] & (1u << other)) != 0 && material[other] == material[bit]) ? 1 : 0;
				}
				if (same > best_count) {
					best_count = same;
					best = material[bit];
				}
			}
			out.push_back(
			    DebrisCluster {
			      .origin = (entries[first].cell * 2) + jitter,
			      .mask = static_cast<uint8_t>(part_masks[p]),
			      .material = best,
			    }
			);
		}
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

}
