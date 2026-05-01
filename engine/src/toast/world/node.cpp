#include "node.hpp"

namespace toast {
Node::Node() {
	m = {
	  .self = Box<Node>(this),
	};
}

[[nodiscard]]
auto Node::listener() -> event::Listener& {
	if (not m.listener) {
		m.listener = std::make_unique<event::Listener>();
	}
	return *m.listener;
}
}
