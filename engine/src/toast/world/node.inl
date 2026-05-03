#pragma once
#include "node.hpp"
#include <toast/world/world.hpp>

namespace toast {
	class World;
template<NodeType T>
auto Node::find(std::string_view path) -> Box<T> {
	// TODO: syntax check
	if ("node:/" == path.substr(0, 7)) {
		// trim "node:/"
	}
	if ("/root" == path.substr(0, 5)) {
		// finding object in root
		return World::find(path);
	}
	if ("/global" == path.substr(0, 7)) {
		// finding object in global
		return World::find(path);
	}
	// Find In Children
	return {};
}

template<NodeType T>
auto Node::search(std::string_view path) -> Box<T> {
	// TODO: syntax check
	if ("node:/" == path.substr(0, 7)) {
		// trim "node:/"
	}
	if ("/root" == path.substr(0, 5)) {
		// finding object in root
		return World::search<Node>(path);
	}
	if ("/global" == path.substr(0, 7)) {
		// finding object in global
		return World::search<Node>(path);
	}
	// Find In Children
	return {};
}

}
