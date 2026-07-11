/**
 * @file node_proxy.hpp
 * @author Xein
 * @date 11 Jul 2026
 *
 * @brief Lua-side proxy for a toast::Node
 */

#pragma once

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

	auto call(const std::string& fn_name, lua_State* L) -> luabridge::LuaRef;

private:
	toast::Box<toast::Node> m_box;
};

// Called AFTER the normal LuaBridge method/property lookup fails
luabridge::LuaRef nodeProxyIndex(NodeProxy& proxy, const luabridge::LuaRef& key, lua_State* L);
luabridge::LuaRef nodeProxyNewindex(NodeProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* L);

}
