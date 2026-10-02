#include "asset_proxy.hpp"

#include "lua_types.hpp"
#include "node_proxy.hpp"
#include "script_context.hpp"

#include <format>
#include <glm/glm.hpp>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <toast/assets/assets.hpp>
#include <toast/assets/data.hpp>
#include <toast/log.hpp>
#include <toast/world/node.hpp>

namespace scripting {

AssetProxy::AssetProxy(toast::UID uid) : m_handle(assets::load(uid)) { }

AssetProxy::AssetProxy(assets::HandleBase handle) : m_handle(std::move(handle)) { }

auto AssetProxy::path() const -> std::string {
	return std::string(m_handle.path());
}

auto AssetProxy::uid() const -> toast::UID {
	return m_handle.uid();
}

auto AssetProxy::hasValue() const -> bool {
	return m_handle.hasValue();
}

auto AssetProxy::type() const -> std::string {
	if (!m_handle.hasValue()) {
		return "";
	}
	return std::string(m_handle->type());
}

auto AssetProxy::toString() const -> std::string {
	return std::format("Asset({} #{})", m_handle.path(), m_handle.uid().get());
}

namespace {

auto camelToSnake(std::string_view name) -> std::string {
	std::string result;
	result.reserve(name.size() + 4);
	for (char c : name) {
		if (std::isupper(static_cast<unsigned char>(c)) && !result.empty()) {
			result += '_';
		}
		result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return result;
}

auto expectedTypeFor(std::string_view field_type) -> std::string {
	if (field_type.starts_with("std::vector<") && field_type.ends_with('>')) {
		field_type.remove_prefix(12);
		field_type.remove_suffix(1);
	}
	constexpr std::string_view prefix = "assets::Handle<";
	if (!field_type.starts_with(prefix) || !field_type.ends_with('>')) {
		return {};
	}
	field_type.remove_prefix(prefix.size());
	field_type.remove_suffix(1);
	if (auto colon = field_type.rfind(':'); colon != std::string_view::npos) {
		field_type = field_type.substr(colon + 1);
	}
	// Prefab::type() returns "node", not "prefab"
	if (field_type == "Prefab") {
		return "node";
	}
	return camelToSnake(field_type);
}

}

auto AssetProxy::checkType(std::string_view field_type) const -> std::string {
	if (m_handle.uid().data() == 0) {
		return {};
	}
	const std::string expected = expectedTypeFor(field_type);
	if (expected.empty()) {
		return {};
	}
	const std::string actual = m_handle.hasValue() ? std::string(m_handle->type()) : assets::typeOf(m_handle.uid());
	if (actual.empty()) {
		TOAST_WARN("Lua", "checkType: asset {} has no manifest entry; cannot validate against '{}'", m_handle.uid(), field_type);
		return {};
	}
	if (actual != expected) {
		return std::format("expected Asset<{}>, got Asset<{}>", expected, actual);
	}
	return {};
}

auto dataValueToLuaRef(lua_State* l, const assets::DataValue& value) -> luabridge::LuaRef {
	using assets::DataType;

	switch (value.type()) {
		case DataType::null: return {l};
		case DataType::bool_t: return {l, value.as<bool>()};
		case DataType::int_t: return {l, static_cast<lua_Integer>(value.as<int64_t>())};
		case DataType::float_t: return {l, static_cast<lua_Number>(value.as<double>())};
		case DataType::string_t: return {l, value.as<std::string>()};
		case DataType::vec2_t: return {l, value.as<glm::vec2>()};
		case DataType::vec3_t: return {l, value.as<glm::vec3>()};
		case DataType::color3_t: return {l, Color3(value.as<glm::vec3>())};
		case DataType::color4_t: return {l, Color4(value.as<glm::vec4>())};

		case DataType::asset_t: return {l, AssetProxy(value.as<toast::UID>())};

		case DataType::node_t: {
			auto owner = currentScriptNode();
			if (!owner.exists()) {
				return {l};
			}
			const auto uid = value.as<toast::UID>();
			if (uid.data() == 0) {
				return {l};
			}
			auto resolved = owner->find(uid);
			if (!resolved.exists()) {
				return {l};
			}
			return {l, NodeProxy(resolved)};
		}

		case DataType::array_t: {
			lua_createtable(l, static_cast<int>(value.size()), 0);
			for (size_t i = 0; i < value.size(); ++i) {
				lua_pushinteger(l, static_cast<lua_Integer>(i + 1));
				dataValueToLuaRef(l, value[i]).push(l);
				lua_settable(l, -3);
			}
			return luabridge::LuaRef::fromStack(l);
		}

		case DataType::object_t: {
			lua_createtable(l, 0, static_cast<int>(value.items().size()));
			for (const auto& [key, field] : value.items()) {
				lua_pushlstring(l, key.data(), key.size());
				dataValueToLuaRef(l, field).push(l);
				lua_settable(l, -3);
			}
			return luabridge::LuaRef::fromStack(l);
		}
	}

	return {l};
}

namespace {

auto dataFieldOrNil(const AssetProxy& proxy, const std::string& name, lua_State* l) -> luabridge::LuaRef {
	if (proxy.type() != "data" || !proxy.hasValue()) {
		return {l};
	}
	const auto& data = static_cast<const assets::Data&>(proxy.handle().get());
	if (!data.root().isObject() || !data.root().contains(name)) {
		return {l};
	}
	return dataValueToLuaRef(l, data.root()[name]);
}

}

auto AssetProxy::get(const std::string& name, lua_State* l) const -> luabridge::LuaRef {
	return dataFieldOrNil(*this, name, l);
}

auto assetProxyIndex(AssetProxy& proxy, const luabridge::LuaRef& key, lua_State* l) -> luabridge::LuaRef {
	if (!key.isString()) {
		return {l};
	}
	return dataFieldOrNil(proxy, key.tostring(), l);
}

auto assetProxyNewindex(AssetProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* l)
    -> luabridge::LuaRef {
	(void)value;
	const std::string name = key.isString() ? key.tostring() : "?";
	luaL_error(
	    l,
	    "Asset '%s' (%s) is read-only; field '%s' cannot be assigned from Lua",
	    proxy.path().c_str(),
	    proxy.type().c_str(),
	    name.c_str()
	);
	return {l};
}

}
