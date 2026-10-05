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
	void addState(const std::string& name, const State& state);

	[[Reflect]]
	void addState(const std::string& name, const luabridge::LuaRef& table);

	[[Reflect]]
	void setState(const std::string& name);

	[[Reflect]]
	auto getState() -> std::string_view {
		return current_state;
	}

protected:
	/// The callbacks are Lua functions, so the machine runs together with the scripts that gave them to it
	[[nodiscard]]
	auto luaAffinity() const noexcept -> std::optional<size_t> override {
		return m_lua_vm;
	}

private:
	[[Reflect, ReadOnly]]
	std::string current_state;

	std::map<std::string, std::unique_ptr<State>> states;
	State* cached_state = nullptr;
	std::optional<size_t> m_lua_vm;    ///< the interpreter the callbacks of the states live on

	void begin();
	void end();
	void tick();
};
}
