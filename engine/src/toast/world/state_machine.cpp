#include "state_machine.hpp"

#include <luabridge3/LuaBridge/LuaBridge.h>
#include <toast/log.hpp>

namespace toast {

auto callFn(auto fn) {
	using ReturnType = std::invoke_result_t<decltype(*fn)>;

	if constexpr (std::is_void_v<ReturnType>) {
		if (fn) {
			(*fn)();
		}
	} else {
		if (fn) {
			return (*fn)();
		}
		return ReturnType {};
	}
}

void StateMachine::begin() {
	setState(default_state);
}

void StateMachine::end() {
	if (not cached_state) {
		return;
	}
	callFn(cached_state->exit);
	cached_state = nullptr;
}

void StateMachine::tick() {
	if (not cached_state) {
		return;
	}
	callFn(cached_state->tick);
	for (const auto& trans : cached_state->transitions) {
		if (callFn(trans.condition)) {
			setState(trans.to);
		}
	}
}

void StateMachine::setState(const std::string& name) {
	if (cached_state) {
		callFn(cached_state->exit);

		for (const auto& trans : cached_state->transitions) {
			if (trans.to == name) {
				callFn(trans.invoke);
				break;
			}
		}
	}

	if (states.contains(name)) {
		cached_state = states[name].get();
	} else {
		TOAST_WARN("StateMachine", "Nodes: {}:{}, NON VALID STATE: {}", this->uid(), this->name(), name);
		cached_state = nullptr;
		return;
	}

	callFn(cached_state->entry);
}

void StateMachine::addState(const std::string& name, const State& state) {
	if (states.contains(name)) { }
	states.emplace(name, std::make_unique<State>(state));
}

auto luaVoidCallback(const luabridge::LuaRef& value, std::string_view name) -> std::optional<std::function<void()>> {
	if (value.isNil()) {
		return std::nullopt;
	}
	if (!value.isFunction()) {
		TOAST_WARN("StateMachine", "State callback '{}' must be a function", name);
		return std::nullopt;
	}

	return [callback = value, callback_name = std::string(name)] {
		auto result = callback();
		if (!result) {
			TOAST_ERROR("StateMachine", "State callback '{}' failed: {}", callback_name, result.errorMessage());
		}
	};
}

auto luaConditionCallback(const luabridge::LuaRef& value) -> std::optional<std::function<bool()>> {
	if (value.isNil()) {
		return std::nullopt;
	}
	if (!value.isFunction()) {
		TOAST_WARN("StateMachine", "Transition condition must be a function");
		return std::nullopt;
	}

	return [callback = value] {
		auto result = callback();
		if (!result) {
			TOAST_ERROR("StateMachine", "Transition condition failed: {}", result.errorMessage());
			return false;
		}
		if (result.size() != 1 || !result[0].isBool()) {
			TOAST_WARN("StateMachine", "Transition condition must return a boolean");
			return false;
		}
		return result[0].unsafe_cast<bool>();
	};
}

auto parseTransition(const luabridge::LuaRef& table) -> std::optional<Transition> {
	if (!table.isTable()) {
		TOAST_WARN("StateMachine", "State transition must be a table");
		return std::nullopt;
	}

	const luabridge::LuaRef target = table["to"];
	if (!target.isString()) {
		TOAST_WARN("StateMachine", "State transition requires a string 'to' field");
		return std::nullopt;
	}

	Transition transition;
	transition.to = target.tostring();
	transition.condition = luaConditionCallback(table["condition"]);
	transition.invoke = luaVoidCallback(table["invoke"], "transition.invoke");
	return transition;
}

void StateMachine::addState(const std::string& name, const luabridge::LuaRef& table) {
	if (!table.isTable()) {
		TOAST_WARN("StateMachine", "addState('{}'): state must be a table", name);
		return;
	}

	State state;
	state.exit = luaVoidCallback(table["exit"], "exit");
	state.entry = luaVoidCallback(table["entry"], "entry");
	state.tick = luaVoidCallback(table["tick"], "tick");

	const luabridge::LuaRef transitions = table["transitions"];
	if (transitions.isTable()) {
		if (transitions[1].isNil()) {
			if (auto transition = parseTransition(transitions)) {
				state.transitions.push_back(std::move(*transition));
			}
		} else {
			for (int index = 1; !transitions[index].isNil(); ++index) {
				if (auto transition = parseTransition(transitions[index])) {
					state.transitions.push_back(std::move(*transition));
				}
			}
		}
	}

	addState(name, state);
}

}    // namespace toast
