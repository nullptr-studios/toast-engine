/**
 * @file fracture_random.hpp
 * @author Xein
 * @date 16 Oct 2026
 * @brief Deterministic randomness for destruction
 */

#pragma once

#include "body.hpp"
#include "shape.hpp"

#include <glm/glm.hpp>
#include <numbers>
#include <toast/hash.hpp>
#include <toast/voxel/voxel_constants.hpp>

namespace physics {

enum class FractureStream : uint8_t {
	edge_noise = 1,
	shatter_points = 2,
	granular = 3,
	debris = 4,
	edge_lobes = 6,
	core_split = 7,
};

[[nodiscard]]
constexpr auto streamSeed(uint64_t seed, FractureStream stream) noexcept -> uint64_t {
	toast::hashCombine(seed, static_cast<uint64_t>(stream));
	return seed;
}

[[nodiscard]]
constexpr auto unitFloat(uint64_t bits) noexcept -> float {
	// this function takes the main 24 bits from a u64 number and converts it
	// to a floating point number on the range [0, 1]
	return static_cast<float>(bits >> 40) * 0x1.0p-24f;
}

inline auto sphereDirection(float u, float v) noexcept -> glm::vec3 {
	float z = (u * 2.0f) - 1.0f;
	float ring = std::sqrt(glm::max(1.0f - (z * z), 0.0f));
	float phi = v * 2.0f * std::numbers::pi_v<float>;
	return {ring * std::cos(phi), ring * std::sin(phi), z};
}

[[nodiscard]]
constexpr auto hashCell(uint64_t seed, glm::ivec3 cell) noexcept -> uint64_t {
	toast::hashCombine(seed, static_cast<uint64_t>(cell.x));
	toast::hashCombine(seed, static_cast<uint64_t>(cell.y));
	toast::hashCombine(seed, static_cast<uint64_t>(cell.z));
	return seed;
}

[[nodiscard]]
inline auto fractureSeed(BodyID source, ShapeID shape, glm::vec3 local_point, uint64_t tick) noexcept -> uint64_t {
	// calculates the seed of one hit
	uint64_t seed = 0;
	toast::hashCombine(seed, (static_cast<uint64_t>(source.generation) << 32) | source.slot);
	toast::hashCombine(seed, (static_cast<uint64_t>(shape.generation) << 32) | source.slot);
	seed = hashCell(seed, glm::ivec3(glm::floor(local_point / voxel::k_voxel_size)));
	toast::hashCombine(seed, tick);
	return seed != 0 ? seed : 1;
}

[[nodiscard]]
constexpr auto hash01(uint64_t seed, glm::ivec3 cell) noexcept -> float {
	return unitFloat(hashCell(seed, cell));
}

[[nodiscard]]
inline auto hashUnit3(uint64_t seed, uint64_t index) noexcept -> glm::vec3 {
	toast::hashCombine(seed, index);
	return sphereDirection(unitFloat(seed), unitFloat(seed << 24));
}

[[nodiscard]]
inline auto valueNoise3(uint64_t seed, glm::vec3 p) noexcept -> float {
	// trilinear of the random
	glm::vec3 base = glm::floor(p);
	glm::ivec3 cell = {base};
	glm::vec3 t = p - base;
	glm::vec3 w = t * t * (3.0f - (2.0f * t));

#define larp(a, b, c) std::lerp(a, b, c)

	auto at = [&](int x, int y, int z) { return hash01(seed, cell + glm::ivec3 {x, y, z}); };
	float x00 = larp(at(0, 0, 0), at(1, 0, 0), w.x);
	float x01 = larp(at(0, 1, 0), at(1, 1, 0), w.x);
	float x10 = larp(at(0, 0, 1), at(1, 0, 1), w.x);
	float x11 = larp(at(0, 1, 1), at(1, 1, 1), w.x);
	return larp(larp(x00, x10, w.y), larp(x01, x11, w.y), w.z);

#undef larp
}

struct Rng {
	uint64_t state = 0;

	explicit constexpr Rng(uint64_t seed) noexcept : state(seed) { }

	constexpr auto next() noexcept -> uint64_t {
		state += toast::k_golden_gamma;
		return toast::splitmix64(state);
	}

	constexpr auto uniform() noexcept -> float { return unitFloat(next()); }

	constexpr auto range(float lo, float hi) noexcept -> float { return lo + ((hi - lo) * uniform()); }

	constexpr auto below(uint32_t count) noexcept -> uint32_t { return static_cast<uint32_t>(((next() >> 32) * count) >> 32); }

	constexpr auto chance(float probability) noexcept -> bool { return uniform() < probability; }

	auto direction() noexcept -> glm::vec3 {
		uint64_t bits = next();
		return sphereDirection(unitFloat(bits), unitFloat(bits << 24));
	}
};

}
