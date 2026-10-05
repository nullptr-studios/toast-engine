#include "state_machine.hpp"

#include <format>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <toast/log.hpp>
#include <toast/scripting/lua_callback.hpp>

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
	ZoneScoped;
	if (not cached_state && current_state.empty()) {
		return;
	}
	if (states.contains(current_state)) {
		cached_state = states.at(current_state).get();
	}
	if (cached_state) {
		callFn(cached_state->entry);
	}
}

void StateMachine::end() {
	ZoneScoped;
	if (not cached_state && current_state.empty()) {
		return;
	}
	if (states.contains(current_state)) {
		cached_state = states.at(current_state).get();
	}
	if (cached_state) {
		callFn(cached_state->exit);
	}
	cached_state = nullptr;
}

void StateMachine::tick() {
	ZoneScoped;
	if (not cached_state) {
		return;
	}
	callFn(cached_state->tick);
	for (const auto& trans : cached_state->transitions) {
		if (callFn(trans.condition)) {
			setState(trans.to);
			break;
		}
	}
}

void StateMachine::setState(const std::string& name) {
	ZoneScoped;
	if (current_state == name) {
		return;
	}
	if (not cached_state) {
		current_state = name;
		return;
	}
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
		current_state = name;
	} else {
		TOAST_WARN("StateMachine", "Nodes: {}:{}, NON VALID STATE: {}", this->uid(), this->name(), name);
		return;
	}

	callFn(cached_state->entry);
}

void StateMachine::addState(const std::string& name, const State& state) {
	if (states.contains(name)) {
		TOAST_WARN("StateMachine", "Overwriting existing state: {}", name);
	}
	if (cached_state != nullptr) {
		TOAST_WARN("StateMachine", "Cannot Add State: {} After initiation", name);
	}
	// A rebuilt script registers its states again, and emplace would keep the old ones with their dead callbacks
	const auto existing = states.find(name);
	const bool replaces_current = existing != states.end() && cached_state == existing->second.get();
	states.insert_or_assign(name, std::make_unique<State>(state));
	if (replaces_current) {
		cached_state = states.at(name).get();
	}
}

auto luaVoidCallback(const luabridge::LuaRef& value, std::string_view name) -> std::optional<std::function<void()>> {
	if (value.isNil()) {
		return std::nullopt;
	}
	if (!value.isFunction()) {
		TOAST_WARN("StateMachine", "State callback '{}' must be a function", name);
		return std::nullopt;
	}

	// A raw LuaRef here would be called without owning its interpreter and could outlive the script that wrote it
	return
	    [callback = scripting::LuaCallback::capture(value, std::format("state callback '{}'", name))] { (void)callback.invoke(); };
}

auto luaConditionCallback(const luabridge::LuaRef& value) -> std::optional<std::function<bool()>> {
	if (value.isNil()) {
		return std::nullopt;
	}
	if (!value.isFunction()) {
		TOAST_WARN("StateMachine", "Transition condition must be a function");
		return std::nullopt;
	}

	return [callback = scripting::LuaCallback::capture(value, "transition condition")] {
		return callback.invokeBool().value_or(false);
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
