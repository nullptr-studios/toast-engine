/// @file   StateMachine.hpp
/// @author Akaansh
/// @date   28/10/25
/// @brief  State Machine Class

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace toast {

template<typename T>
class StateMachine;

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
template<typename T>
struct State {
	virtual void OnBegin() { };
	virtual void OnTick() { };
	virtual void OnExit() { };

	T* parent = nullptr;
};

/**
 * @class StateMachine
 * @brief A finite state machine using string identifiers.
 *
 * The StateMachine class stores a collection of named states and also manages transitions between them.
 * Each state can define OnEnter, OnUpdate, and OnExit functions.
 * The machine calls OnExit() on the old state before switching, and OnEnter() on the new one.
 */
 template<typename T>
class StateMachine {
public:
	StateMachine() = default;

	void SetParent(T* parent) { m.parent = parent; }

	void AddState(const std::string& name, std::unique_ptr<State<T>>&& state);
	void SetState(const std::string& name);
	void Tick();

	const std::string& GetCurrentState() const;

private:
	struct {
		std::unordered_map<std::string, std::unique_ptr<State<T>>> states;
		std::string currentState;
		T* parent = nullptr;
	} m;
};

template<typename T>
void StateMachine<T>::AddState(const std::string& name, std::unique_ptr<State<T>>&& state) {
	state->parent = m.parent;
	m.states[name] = std::move(state);
}

template<typename T>
void StateMachine<T>::SetState(const std::string& name) {
	if (m.currentState == name) {
		return;
	}

	// Call OnExit on the previous if it existss
	if (!m.currentState.empty()) {
		auto it = m.states.find(m.currentState);
		if (it != m.states.end()) {
			it->second->OnExit();
		}
	}

	// Set the new State
	m.currentState = name;

	// Call OnEnter on the new State
	auto new_it = m.states.find(m.currentState);
	if (new_it != m.states.end()) {
		new_it->second->OnBegin();
	}
}

// Run OnUpdate with the deltaTime each frame
template<typename T>
void StateMachine<T>::Tick() {
	auto it = m.states.find(m.currentState);
	if (it != m.states.end()) {
		it->second->OnTick();
	}
}

// Getter
template<typename T>
const std::string& StateMachine<T>::GetCurrentState() const {
	return m.currentState;
}

}
