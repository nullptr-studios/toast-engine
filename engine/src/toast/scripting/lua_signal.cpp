#include "lua_signal.hpp"

namespace scripting {

LuaSignal::LuaSignal() : m_state(std::make_shared<State>()) { }

void LuaSignal::owner(toast::Box<toast::Node> node) {
	m_state->owner = std::move(node);
}

auto LuaSignal::connect(NodeProxy target, std::string_view function, signals::ConnectionSource source, bool forwards_args)
    -> bool {
	if (!m_state->owner.exists() || !target.exists() || function.empty()) {
		return false;
	}
	for (const auto& connection : m_state->signal.connections()) {
		if (connection.target == target.box()->uid() && connection.function == function && connection.source == source) {
			return false;
		}
	}
	m_state->signal.connect(*target.box(), function, source, forwards_args);
	return true;
}

auto LuaSignal::disconnect(NodeProxy target, std::string_view function, signals::ConnectionSource source) -> bool {
	if (!target.exists() || function.empty()) {
		return false;
	}
	const auto connections = m_state->signal.connections();
	const bool exists = std::ranges::any_of(connections, [&](const auto& connection) {
		return connection.target == target.box()->uid() && connection.function == function && connection.source == source;
	});
	if (!exists) {
		return false;
	}
	m_state->signal.disconnect(*target.box(), function, source);
	return true;
}

auto LuaSignal::connectSelf(std::string_view function, signals::ConnectionSource source, bool forwards_args) -> bool {
	return connect(NodeProxy(m_state->owner), function, source, forwards_args);
}

auto LuaSignal::disconnectSelf(std::string_view function, signals::ConnectionSource source) -> bool {
	return disconnect(NodeProxy(m_state->owner), function, source);
}

}
