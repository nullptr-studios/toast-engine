#include "ui_binds_proxy.hpp"

#include <RmlUi/Core/Variant.h>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <toast/log.hpp>
#include <toast/ui/ui_binds.hpp>
#include <toast/world/node.hpp>

namespace scripting {

namespace {

auto variantToLuaRef(lua_State* l, const Rml::Variant& v) -> luabridge::LuaRef {
	switch (v.GetType()) {
		case Rml::Variant::BOOL: return {l, v.Get<bool>()};
		case Rml::Variant::BYTE:
		case Rml::Variant::CHAR:
		case Rml::Variant::INT: return {l, static_cast<lua_Integer>(v.Get<int>())};
		case Rml::Variant::INT64:
		case Rml::Variant::UINT:
		case Rml::Variant::UINT64: return {l, static_cast<lua_Integer>(v.Get<int64_t>())};
		case Rml::Variant::FLOAT:
		case Rml::Variant::DOUBLE: return {l, static_cast<lua_Number>(v.Get<double>())};
		case Rml::Variant::NONE: return {l};
		// STRING and anything else round-trips through the string converter
		default: return {l, v.Get<Rml::String>()};
	}
}

auto luaRefToVariant(lua_State* l, const luabridge::LuaRef& v) -> Rml::Variant {
	if (v.isBool()) {
		return Rml::Variant(v.unsafe_cast<bool>());
	}
	if (v.isNumber()) {
		v.push(l);
		const bool is_int = lua_isinteger(l, -1) != 0;
		lua_pop(l, 1);
		if (is_int) {
			return Rml::Variant(static_cast<int>(v.unsafe_cast<lua_Integer>()));
		}
		return Rml::Variant(static_cast<double>(v.unsafe_cast<lua_Number>()));
	}
	if (v.isString()) {
		return Rml::Variant(Rml::String(v.tostring()));
	}
	return {};
}

auto resolveBinds(const toast::Node* node) -> ui::UIBinds* {
	return node != nullptr ? ui::UIBinds::forNode(node) : nullptr;
}

}

UIBindsProxy::UIBindsProxy(toast::Box<toast::Node> box) noexcept : m_box(std::move(box)) { }

auto UIBindsProxy::exists() const noexcept -> bool {
	return m_box.exists();
}

auto uiBindsProxyIndex(UIBindsProxy& proxy, const luabridge::LuaRef& key, lua_State* l) -> luabridge::LuaRef {
	if (!proxy.exists()) {
		luaL_error(l, "ui_binds: panel reference is dead");
		return {l};
	}
	if (!key.isString()) {
		return {l};
	}

	const std::string name = key.tostring();
	ui::UIBinds* binds = resolveBinds(proxy.box().operator->());
	if (binds == nullptr || !binds->has(name)) {
		return {l};
	}
	return variantToLuaRef(l, binds->get(name));
}

auto uiBindsProxyNewindex(UIBindsProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* l)
    -> luabridge::LuaRef {
	if (!proxy.exists()) {
		luaL_error(l, "ui_binds: panel reference is dead");
		return {l};
	}
	if (!key.isString()) {
		luaL_error(l, "ui_binds: bind name must be a string");
		return {l};
	}

	const std::string name = key.tostring();
	ui::UIBinds* binds = resolveBinds(proxy.box().operator->());
	if (binds == nullptr) {
		luaL_error(l, "ui_binds: panel has no data model");
		return {l};
	}
	if (!binds->has(name)) {
		// A write to an unknown bind is almost always a typo
		TOAST_WARN("UI", "ui_binds: '{}' is not a bind in this document", name);
		return {l};
	}

	binds->set(name, luaRefToVariant(l, value));
	return {l};
}

auto uiBindsHas(const toast::Node* node, std::string_view name) -> bool {
	ui::UIBinds* binds = resolveBinds(node);
	return binds != nullptr && binds->has(name);
}

auto uiBindsGet(const toast::Node* node, std::string_view name, lua_State* l) -> luabridge::LuaRef {
	ui::UIBinds* binds = resolveBinds(node);
	if (binds == nullptr || !binds->has(name)) {
		return {l};
	}
	return variantToLuaRef(l, binds->get(name));
}

auto uiBindsSet(toast::Node* node, std::string_view name, const luabridge::LuaRef& value, lua_State* l) -> bool {
	ui::UIBinds* binds = resolveBinds(node);
	if (binds == nullptr || !binds->has(name)) {
		return false;
	}
	binds->set(name, luaRefToVariant(l, value));
	return true;
}

}
