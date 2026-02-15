/**
 * @file ColliderFlags.hpp
 * @author Iñaki
 * @date 19/01/2026
 *
 */
#pragma once

// clang-format off
enum class ColliderFlags : uint8_t {
	Default = 0b00000,
	Ground  = 0b00001,
	Player  = 0b00010,
	Enemy   = 0b00100,
	Ramp    = 0b01000,
	Weapon  = 0b10000
};
// clang-format on

inline const char* to_string(ColliderFlags e) {
	switch (e) {
		case ColliderFlags::Default: return "Default";
		case ColliderFlags::Ground: return "Ground";
		case ColliderFlags::Player: return "Player";
		case ColliderFlags::Enemy: return "Enemy";
		case ColliderFlags::Ramp: return "Ramp";
		case ColliderFlags::Weapon: return "Weapon";
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

inline bool operator!(ColliderFlags a) {
	using T = std::underlying_type_t<ColliderFlags>;
	return static_cast<T>(a) == 0;
}

inline ColliderFlags& operator|=(ColliderFlags& a, ColliderFlags b) {
	a = a | b;
	return a;
}

inline ColliderFlags& operator&=(ColliderFlags& a, ColliderFlags b) {
	a = a & b;
	return a;
}
