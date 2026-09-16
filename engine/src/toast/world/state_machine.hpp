/**
 * @file state_machine.hpp
 * @author Dante Harper
 * @date 15 Sep 26
 */

#pragma once

#include "toast/world/node.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace toast {
struct Transition {
	std::string to;
	std::optional<std::function<bool()>> condition;
	std::optional<std::function<void()>> invoke;
};

struct State {
	std::optional<std::function<void()>> exit;
	std::optional<std::function<void()>> entry;
	std::optional<std::function<void()>> tick;
	std::vector<Transition> transitions;
};

class [[ToastNode]] TOAST_API StateMachine : public Node {
public:
	[[Reflect]]
	std::string default_state;

	[[Reflect, ReadOnly]]
	std::string current_state;

	void addState(std::string_view name, const State& state) { }

private:
};
}
