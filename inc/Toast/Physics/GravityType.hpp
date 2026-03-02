/// @file GravityType.hpp
/// @author Xein
/// @date 28 Feb 2026

#pragma once
#include <string>
#include <string_view>

namespace physics {

struct GravityType {
	enum type {
		DIRECTION,
		POINT
	};

	type v;

	GravityType(type value) : v(value) { }

	GravityType(const GravityType& other) = default;

	auto operator=(type value) -> GravityType&;
	auto operator=(const GravityType& other) -> GravityType& = default;
	bool operator==(type value) const;
	bool operator==(const GravityType& other) const;

	static auto FromString(std::string_view other) -> GravityType;
	static auto ToString(GravityType other) -> std::string;
};

}
