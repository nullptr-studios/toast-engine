/**
 * @file ColliderFlags.hpp
 * @author Iñaki
 * @date 19/01/2026
 *
 */
#pragma once

enum class ColliderFlags : uint8_t {
	Default = 0b0000,
	Ground = 0b0001,
	Player = 0b0010,
	Enemy = 0b0100
};

inline bool operator|(ColliderFlags lhs, ColliderFlags rhs) {
	return static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs);
}

inline bool operator&(ColliderFlags lhs, ColliderFlags rhs) {
	return static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs);
}



