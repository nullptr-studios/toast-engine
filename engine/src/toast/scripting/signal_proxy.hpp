#pragma once

#include "toast/reflect/reflect.hpp"
#include "toast/scripting/node_proxy.hpp"

#include <string_view>

struct lua_State;

namespace scripting {
class SignalProxy {
public:
	SignalProxy(toast::Box<toast::Node> source, const toast::SignalInfo& info) : m_source(std::move(source)), m_info(&info) { }

	[[nodiscard]]
	auto connect(NodeProxy target, std::string_view function, signals::ConnectionSource source, bool forwards_args = true) -> bool;

	[[nodiscard]]
	auto disconnect(NodeProxy target, signals::ConnectionSource source, std::string_view function) -> bool;

	[[nodiscard]]
	auto connectSelf(std::string_view function, signals::ConnectionSource source, bool forwards_args = true) -> bool;

	[[nodiscard]]
	auto disconnectSelf(signals::ConnectionSource source, std::string_view function) -> bool;

	void clear(signals::ConnectionSource source);

	[[nodiscard]]
	auto fire(lua_State* state) -> bool;

private:
	toast::Box<toast::Node> m_source;
	const toast::SignalInfo* m_info;
};
}
