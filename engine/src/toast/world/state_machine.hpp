/**
 * @file state_machine.hpp
 * @author Dante Harper
 * @date 15 Sep 26
 */

#pragma once

#include "toast/log.hpp"
#include "toast/world/node.hpp"

#include <functional>
#include <lua.hpp>
#include <luabridge3/LuaBridge/detail/LuaRef.h>
#include <map>
#include <memory>
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

	void addState(const std::string& name, const State& state);

	[[Reflect]]
	void addState(const std::string& name, const luabridge::LuaRef& table);

	void setState(const std::string& name);

private:
	std::map<std::string, std::unique_ptr<State>> states;
	State* cached_state = nullptr;

	void begin();
	void end();
	void tick();
};
}
