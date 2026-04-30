/**
 * @file node.hpp
 * @author Dante Harper
 * @date 29 Apr 26
 *
 * @brief [TODO: Brief description of the file's purpose]
 */

#pragma once

#include "toast/events/listener.hpp"
#include "toast/world/registry.hpp"

#include <memory>

namespace toast {
class Node;

class Node {
	struct {
		NodeVTable& v_table;
		std::unique_ptr<event::Listener> listener;

	} m;

public:
	[[nodiscard]]
	auto listener() -> event::Listener& {
		if (not m.listener) {
			m.listener = std::make_unique<event::Listener>();
		}
		return *m.listener;
	}
};
}
