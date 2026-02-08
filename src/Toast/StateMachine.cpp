#include "Toast/StateMachine.hpp"

#include "Toast/Time.hpp"

namespace toast {

StateMachine::StateMachine() = default;

void StateMachine::AddState(const std::string& name, State&& state) {
	m.states[name] = state;
}

void StateMachine::SetState(const std::string& name) {
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
void StateMachine::Tick() {
	auto it = m.states.find(m.currentState);    // replaced std::unordered_map<std::string, State>::iterator with auto
	if (it != m.states.end() && it->second.onTick) {
		it->second.onTick();
	}
}

// Getter
const std::string& StateMachine::GetCurrentState() const {
	return m.currentState;
}

}
