/// @file   StateMachine.hpp
/// @author Akaansh
/// @date   28/10/25
/// @brief  State Machine Class

#pragma once

#include <functional>
#include <string>
#include <unordered_map>

namespace toast {

class StateMachineOld;

/**
 * @struct State
 * @brief Represents a single state in the finite state machine.
 *
 * Each function is optional, if it's empty, nothing happens.
 * - onBegin(): called once as soon as the state becomes active.
 * - onTick(): called every frame while the state is active.
 * - onExit(): called just before switching to the next state.
 *
 * These use std::function so that you can assign lambdas or normal functions.
 */
struct [[deprecated("Use State")]] StateOld {
	std::function<void()> onBegin;
	std::function<void()> onTick;
	std::function<void()> onExit;
};

/**
 * @class StateMachine
 * @brief A finite state machine using string identifiers.
 *
 * The StateMachine class stores a collection of named states and also manages transitions between them.
 * Each state can define OnEnter, OnUpdate, and OnExit functions.
 * The machine calls OnExit() on the old state before switching, and OnEnter() on the new one.
 */
class [[deprecated("Use StateMachine")]] StateMachineOld {
public:
	StateMachineOld();

	void AddState(const std::string& name, StateOld&& state);
	void SetState(const std::string& name);
	void Tick();

	const std::string& GetCurrentState() const;

private:
	struct {
		std::unordered_map<std::string, StateOld> states;
		std::string currentState;
	} m;
};

}
