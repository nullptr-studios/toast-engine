#pragma once

#include "toast/events/signals.hpp"
#include "toast/scripting/node_proxy.hpp"

#include <memory>
#include <string>
#include <vector>

namespace scripting {

class LuaSignal {
public:
	LuaSignal();

	void owner(toast::Box<toast::Node> node);

	[[nodiscard]]
	auto connect(NodeProxy target, std::string_view function, signals::ConnectionSource source, bool forwards_args = true) -> bool;

	[[nodiscard]]
	auto disconnect(NodeProxy target, std::string_view function, signals::ConnectionSource source) -> bool;

	[[nodiscard]]
	auto connectSelf(std::string_view function, signals::ConnectionSource source, bool forwards_args = true) -> bool;

	[[nodiscard]]
	auto disconnectSelf(std::string_view function, signals::ConnectionSource source) -> bool;

	void clear(signals::ConnectionSource source) { m_state->signal.clear(source); }

	void fire(signals::DynamicArgs args = {}) { m_state->signal.fire(args); }

	void argTypes(std::vector<std::string> types) { m_state->arg_types = std::move(types); }

	[[nodiscard]]
	auto argTypes() const -> const std::vector<std::string>& {
		return m_state->arg_types;
	}

	[[nodiscard]]
	auto connections() const -> std::vector<signals::ConnectionInfo> {
		return m_state->signal.connections();
	}

private:
	struct State {
		toast::Box<toast::Node> owner;
		signals::Signal<signals::DynamicArgs> signal;
		std::vector<std::string> arg_types;
	};

	std::shared_ptr<State> m_state;
};

}
