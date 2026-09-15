/**
 * @file ui_binds_proxy.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Lua handle for a Panel data binds
 */

#pragma once
#include <string_view>
#include <toast/world/box.hpp>

struct lua_State;

namespace luabridge {
class LuaRef;
}

namespace toast {
class Node;
}

namespace scripting {

/**
 * @brief Lua type returned by `self.ui_binds`
 */
class UIBindsProxy {
public:
	UIBindsProxy() = default;
	explicit UIBindsProxy(toast::Box<toast::Node> box) noexcept;

	[[nodiscard]]
	auto exists() const noexcept -> bool;

	[[nodiscard]]
	auto box() const noexcept -> const toast::Box<toast::Node>& {
		return m_box;
	}

private:
	toast::Box<toast::Node> m_box;
};

auto uiBindsProxyIndex(UIBindsProxy& proxy, const luabridge::LuaRef& key, lua_State* l) -> luabridge::LuaRef;
auto uiBindsProxyNewindex(UIBindsProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* l)
    -> luabridge::LuaRef;

// `self.<bind>` works the same as `self.ui_binds.<bind>` TODO: maybe remove this
[[nodiscard]]
auto uiBindsHas(const toast::Node* node, std::string_view name) -> bool;
auto uiBindsGet(const toast::Node* node, std::string_view name, lua_State* l) -> luabridge::LuaRef;
auto uiBindsSet(toast::Node* node, std::string_view name, const luabridge::LuaRef& value, lua_State* l) -> bool;

}
