/**
 * @file node_proxy.hpp
 * @author Xein
 * @date 11 Jul 2026
 *
 * @brief Lua-side proxy for a toast::Node
 */

#pragma once

#include <any>
#include <string>
#include <toast/world/box.hpp>
#include <vector>

struct lua_State;

namespace luabridge {
class LuaRef;
}

namespace toast {
class Node;
}

namespace scripting {

class NodeProxy {
public:
	NodeProxy() = default;
	explicit NodeProxy(toast::Box<toast::Node> box) noexcept;

	[[nodiscard]]
	auto box() noexcept -> toast::Box<toast::Node>& {
		return m_box;
	}

	[[nodiscard]]
	auto box() const noexcept -> const toast::Box<toast::Node>& {
		return m_box;
	}

	[[nodiscard]]
	auto exists() const noexcept -> bool;

	auto find(const std::string& query, lua_State* L) -> luabridge::LuaRef;
	auto search(const std::string& query) -> std::vector<NodeProxy>;
	auto create(const std::string& type, lua_State* L) -> luabridge::LuaRef;
	void addDependsOn(const NodeProxy& other);

	[[nodiscard]]
	auto name() const -> std::string;
	[[nodiscard]]
	auto uid() const -> uint64_t;

	/// Invokes the named reflected C++ method
	/// and fans out to any same-named Lua function on the node's ScriptRuntime
	auto call(const std::string& fn_name, lua_State* L) -> luabridge::LuaRef;

	/// @returns true if the wrapped node has a reflected field with the given name
	[[nodiscard]]
	auto hasField(std::string_view key) const noexcept -> bool;

private:
	toast::Box<toast::Node> m_box;
};

// Called AFTER the normal LuaBridge method/property lookup fails
luabridge::LuaRef nodeProxyIndex(NodeProxy& proxy, const luabridge::LuaRef& key, lua_State* L);
luabridge::LuaRef nodeProxyNewindex(NodeProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* L);

int nodeProxyDispatchMethod(NodeProxy& np, std::string_view name, lua_State* L, int args_base, int n_args);

luabridge::LuaRef anyValueToLuaRef(lua_State* L, const std::any& value);

/// Converts a LuaRef to a std::any by dispatching on the Lua type
std::any luaRefValueToAny(lua_State* L, const luabridge::LuaRef& v);

}
