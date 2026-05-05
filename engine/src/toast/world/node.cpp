#include "node.hpp"

#include "world.hpp"

namespace toast {
Node::Node() {
	m = {
	  .box = Box<Node>(this),
	};
}

auto Node::listener() noexcept -> event::Listener& {
	if (not m.listener) {
		m.listener = std::make_unique<event::Listener>(m.enabled);
	}
	return *m.listener;
}

void Node::enabled(bool state) noexcept {
	m.enabled = state;
	m.listener->enabled(state);
}

auto Node::enabled() const noexcept -> bool {
	return m.enabled;
}

void Node::name(std::string_view name) noexcept {
	m.name = name;
}

[[nodiscard]]
auto Node::name() const noexcept -> std::string {
	return m.name;
}

auto Node::find(std::string_view path) -> Box<Node> {
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

auto Node::search(std::string_view path) -> Box<Node> {
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
