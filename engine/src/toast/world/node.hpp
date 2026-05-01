/**
 * @file node.hpp
 * @author Dante Harper
 * @date 29 Apr 26
 */

#pragma once

#include "toast/events/listener.hpp"
#include "toast/world/box.hpp"
#include "toast/world/registry.hpp"

#include <memory>

namespace toast {

class TOAST_API Node {
	friend struct _detail::ControlBox;

	struct {
		Box<Node> self;
		NodeVTable* v_table;
		std::unique_ptr<event::Listener> listener;

	} m;

public:
	Node();

	[[nodiscard]]
	auto listener() -> event::Listener&;
};
}
