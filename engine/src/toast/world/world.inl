#pragma once
#include "world.hpp"

namespace toast {

template<NodeType T>
auto World::find(std::string_view path) -> Box<T> {
	// TODO: syntax check
	if ("node:/" == path.substr(0, 7)) {
		// trim "node:/"
	}
	if ("/root" == path.substr(0, 5)) {
		// finding object in root
	}
	if ("/global" == path.substr(0, 7)) {
		// finding object in global
	}
	TOAST_ERROR(World, "Invalid Object Path");
	return {};
}

template<NodeType T>
auto World::search(std::string_view path) -> Box<T> {
	// TODO: syntax check
	if ("node:/" == path.substr(0, 7)) {
		// trim "node:/"
	}
	if ("/root" == path.substr(0, 5)) {
		// finding object in root
	}
	if ("/global" == path.substr(0, 7)) {
		// finding object in global
	}
	TOAST_ERROR(World, "Invalid Object Path");
	return {};
}

}
