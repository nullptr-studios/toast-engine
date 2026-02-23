#include "Toast/StateMachineOld.hpp"

#include "Toast/Time.hpp"

namespace toast {

StateMachineOld::StateMachineOld() = default;

void StateMachineOld::AddState(std::string_view name, StateOld&& state) {
	m.states[std::string(name)] = state;
}

void StateMachineOld::SetState(std::string_view name) {
	if (m.currentState == name) {
		return;
	}

	// Call OnExit on the previous if it existss
	if (!m.currentState.empty()) {
		auto it = m.states.find(m.currentState);    // replaced std::unordered_map<std::string, State>::iterator with auto
		if (it != m.states.end() && it->second.onExit) {
			it->second.onExit();
		}
	}

	// Set the new State
	m.currentState = name;

	// Call OnEnter on the new State
	auto new_it = m.states.find(m.currentState);    // replaced std::unordered_map<std::string, State>::iterator with auto
	if (new_it != m.states.end() && new_it->second.onBegin) {
		new_it->second.onBegin();
	}
}

// Run OnUpdate with the deltaTime each frame
void StateMachineOld::Tick() {
	auto it = m.states.find(m.currentState);    // replaced std::unordered_map<std::string, State>::iterator with auto
	if (it != m.states.end() && it->second.onTick) {
		it->second.onTick();
	}
}

// Getter
const std::string& StateMachineOld::GetCurrentState() const {
	return m.currentState;
}

}
