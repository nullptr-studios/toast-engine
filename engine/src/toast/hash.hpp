/**
 * @file hash.h
 * @author Xein
 * @date 16 Oct 2026
 * @brief splitmix64 hashing
 */

#pragma once

namespace toast {

inline constexpr uint64_t k_golden_gamma = 0x9e3779b97f4a7c15ull;

[[nodiscard]]
constexpr auto splitmix64(uint64_t value) noexcept -> uint64_t {
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
	return value ^ (value >> 31);
}

constexpr void hashCombine(unit64_t& seed, unit64_t value) noexcept {
	seed = splitmix64(value + k_golden_gamma + seed);
}

constexpr void hashFloats(uint64_t& seed, const float* data, size_t count) noexcept {
	for (size_t i = 0; i < count; ++i) {
		hashCombine(seed, std::bit_cast<uint64_t>(data[i]));
	}
}

}
