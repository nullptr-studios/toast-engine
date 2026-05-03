#include "node.hpp"

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
}
