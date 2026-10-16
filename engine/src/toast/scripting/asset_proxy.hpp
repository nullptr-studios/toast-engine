/**
 * @file asset_proxy.hpp
 * @author Xein
 * @date 11 Jul 2026
 * @brief Lua-side proxy for an asset handle, registered as "Asset" in Lua
 */

#pragma once

#include <glm/glm.hpp>
#include <string>
#include <toast/assets/core_types.hpp>
#include <toast/uid.hpp>

struct lua_State;

namespace luabridge {
class LuaRef;
}

namespace assets {
class DataValue;
}

namespace scripting {

class AssetProxy {
public:
	AssetProxy() = default;
	explicit AssetProxy(toast::UID uid);               ///< Create from UID
	explicit AssetProxy(assets::HandleBase handle);    ///< Create from another handle

	[[nodiscard]]
	auto path() const -> std::string;
	[[nodiscard]]
	auto uid() const -> toast::UID;
	[[nodiscard]]
	auto hasValue() const -> bool;
	[[nodiscard]]
	auto type() const -> std::string;
	[[nodiscard]]
	auto toString() const -> std::string;
	[[nodiscard]]
	auto checkType(std::string_view field_type) const -> std::string;

	[[nodiscard]]
	auto get(const std::string& name, lua_State* l) const -> luabridge::LuaRef;

	/// Curve assets: the value at @p t, a number for a 1D curve, a vec2 for a 2D one and a vec3 for a 3D one
	[[nodiscard]]
	auto handle() const noexcept -> const assets::HandleBase& {
		return m_handle;
	}

private:
	assets::HandleBase m_handle;
};

// Called AFTER the normal luabridge method fails
auto assetProxyIndex(AssetProxy& proxy, const luabridge::LuaRef& key, lua_State* l) -> luabridge::LuaRef;

// Data assets are read-only from Lua, trying to modify them will just panic, if someone complains about this
// im killing them (hi dario)
auto assetProxyNewindex(AssetProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* l)
    -> luabridge::LuaRef;

auto dataValueToLuaRef(lua_State* l, const assets::DataValue& value) -> luabridge::LuaRef;

}
