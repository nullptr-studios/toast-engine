/**
 * @file id.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Compile-time hashed parameter ids
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace toast::params {

constexpr auto fnv1a(std::string_view text) noexcept -> uint64_t {
	uint64_t hash = 14695981039346656037ULL;
	for (const char c : text) {
		hash ^= static_cast<uint8_t>(c);
		hash *= 1099511628211ULL;
	}
	return hash;
}

struct Id {
	std::string_view text;
	uint64_t hash;

	template<size_t N>
	consteval Id(const char (&literal)[N]) : text(literal, N - 1),
	                                         hash(fnv1a(text)) { }    // NOLINT(google-explicit-constructor)
};

}
