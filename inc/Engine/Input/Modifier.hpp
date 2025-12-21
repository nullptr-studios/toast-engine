/// @file Modifier.hpp
/// @date 13 Dec 2025
/// @author Xein

#pragma once

#include <stdexcept>

namespace input {

struct IModifier {
	IModifier() {
		throw std::runtime_error("Not implemented");
	}
};

}
