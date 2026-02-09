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
	Enemy = 0b0100,
	Ramp = 0b1000
};

inline const char* to_string(ColliderFlags e) {
	switch (e) {
		case ColliderFlags::Default: return "Default";
		case ColliderFlags::Ground: return "Ground";
		case ColliderFlags::Player: return "Player";
		case ColliderFlags::Enemy: return "Enemy";
		case ColliderFlags::Ramp: return "Ramp";
		default: return "unknown";
	}
}

inline ColliderFlags operator|(ColliderFlags a, ColliderFlags b) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<ColliderFlags>(static_cast<T>(a) | static_cast<T>(b));
}

inline ColliderFlags operator&(ColliderFlags a, ColliderFlags b) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<ColliderFlags>(static_cast<T>(a) & static_cast<T>(b));
}

inline ColliderFlags operator~(ColliderFlags a) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<ColliderFlags>(~static_cast<T>(a));
}

// Compound assignment operators (optional but convenient)
inline ColliderFlags& operator|=(ColliderFlags& a, ColliderFlags b) {
	a = a | b;
	return a;
}

inline ColliderFlags& operator&=(ColliderFlags& a, ColliderFlags b) {
	a = a & b;
	return a;
}
