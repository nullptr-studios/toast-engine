#include "signal_proxy.hpp"

#include "asset_proxy.hpp"
#include "lua_types.hpp"
#include "toast/world/node.hpp"

#include <any>
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <string>
#include <string_view>
#include <toast/events/signal_types.hpp>
#include <vector>

namespace scripting {
namespace {

auto luaArgument(lua_State* state, int index, const std::type_info& type) -> std::any {
	if (type == typeid(bool) && lua_isboolean(state, index)) {
		return static_cast<bool>(lua_toboolean(state, index));
	}
	if (lua_isnumber(state, index)) {
		if (type == typeid(int)) {
			return static_cast<int>(lua_tointeger(state, index));
		}
		if (type == typeid(int8_t)) {
			return static_cast<int8_t>(lua_tointeger(state, index));
		}
		if (type == typeid(int16_t)) {
			return static_cast<int16_t>(lua_tointeger(state, index));
		}
		if (type == typeid(int32_t)) {
			return static_cast<int32_t>(lua_tointeger(state, index));
		}
		if (type == typeid(int64_t)) {
			return static_cast<int64_t>(lua_tointeger(state, index));
		}
		if (type == typeid(unsigned int)) {
			return static_cast<unsigned int>(lua_tointeger(state, index));
		}
		if (type == typeid(uint8_t)) {
			return static_cast<uint8_t>(lua_tointeger(state, index));
		}
		if (type == typeid(uint16_t)) {
			return static_cast<uint16_t>(lua_tointeger(state, index));
		}
		if (type == typeid(uint32_t)) {
			return static_cast<uint32_t>(lua_tointeger(state, index));
		}
		if (type == typeid(uint64_t)) {
			return static_cast<uint64_t>(lua_tointeger(state, index));
		}
		if (type == typeid(float)) {
			return static_cast<float>(lua_tonumber(state, index));
		}
		if (type == typeid(double)) {
			return static_cast<double>(lua_tonumber(state, index));
		}
	}
	if (type == typeid(std::string) && lua_isstring(state, index)) {
		size_t length = 0;
		const char* value = lua_tolstring(state, index, &length);
		return std::string(value, length);
	}
	if (type == typeid(std::string_view) && lua_isstring(state, index)) {
		size_t length = 0;
		const char* value = lua_tolstring(state, index, &length);
		return std::string_view(value, length);
	}
	if (type == typeid(glm::vec2)) {
		if (auto value = luabridge::Stack<glm::vec2>::get(state, index)) {
			return *value;
		}
	}
	if (type == typeid(glm::vec3)) {
		if (auto value = luabridge::Stack<glm::vec3>::get(state, index)) {
			return *value;
		}
	}
	if (type == typeid(glm::vec4)) {
		if (auto value = luabridge::Stack<glm::vec4>::get(state, index)) {
			return *value;
		}
	}
	if (type == typeid(glm::quat)) {
		if (auto value = luabridge::Stack<glm::quat>::get(state, index)) {
			return *value;
		}
	}
	if (type == typeid(Color3)) {
		if (auto value = luabridge::Stack<Color3>::get(state, index)) {
			return *value;
		}
	}
	if (type == typeid(Color4)) {
		if (auto value = luabridge::Stack<Color4>::get(state, index)) {
			return *value;
		}
	}
	if (type == typeid(toast::Box<toast::Node>)) {
		if (auto value = luabridge::Stack<NodeProxy>::get(state, index); value && (*value).exists()) {
			return toast::Box<toast::Node>((*value).box());
		}
	}
	return {};
}

}

auto SignalProxy::connect(NodeProxy target, std::string_view function, signals::ConnectionSource source, bool forwards_args)
    -> bool {
	if (!m_source.exists() || !m_info || !m_info->get || !m_info->connect || !target.exists() || function.empty()) {
		return false;
	}
	for (const auto& connection : m_info->get(&*m_source)) {
		if (connection.target == target.box()->uid() && connection.function == function && connection.source == source) {
			return false;
		}
	}
	m_info->connect(&*m_source, *target.box(), function, source, forwards_args);
	return true;
}

auto SignalProxy::disconnect(NodeProxy target, signals::ConnectionSource source, std::string_view function) -> bool {
	if (!m_source.exists() || !m_info || !m_info->disconnect || !target.exists() || function.empty()) {
		return false;
	}
	const auto connections = m_info->get ? m_info->get(&*m_source) : std::vector<signals::ConnectionInfo> {};
	const bool exists = std::ranges::any_of(connections, [&](const signals::ConnectionInfo& connection) {
		return connection.target == target.box()->uid() && connection.function == function && connection.source == source;
	});
	if (!exists) {
		return false;
	}
	m_info->disconnect(&*m_source, *target.box(), function, source);
	return true;
}

auto SignalProxy::connectSelf(std::string_view function, signals::ConnectionSource source, bool forwards_args) -> bool {
	return connect(NodeProxy(m_source), function, source, forwards_args);
}

auto SignalProxy::disconnectSelf(signals::ConnectionSource source, std::string_view function) -> bool {
	return disconnect(NodeProxy(m_source), source, function);
}

void SignalProxy::clear(signals::ConnectionSource source) {
	if (m_source.exists() && m_info && m_info->clear) {
		m_info->clear(&*m_source, source);
	}
}

auto SignalProxy::fire(lua_State* state) -> bool {
	if (!m_source.exists() || !m_info || !m_info->fire) {
		return false;
	}
	const std::size_t argument_count = lua_gettop(state) - 1;
	if (argument_count != m_info->args.size()) {
		return false;
	}

	std::vector<std::any> args;
	args.reserve(m_info->args.size());
	for (size_t i = 0; i < m_info->args.size(); ++i) {
		if (!m_info->args[i]) {
			return false;
		}
		std::any value = luaArgument(state, static_cast<int>(i) + 2, *m_info->args[i]);
		if (!value.has_value()) {
			return false;
		}
		args.push_back(std::move(value));
	}
	return m_info->fire(&*m_source, args);
}

}
