#include "ui_binds_proxy.hpp"

#include "ui_bind_convert.hpp"

#include <RmlUi/Core/Variant.h>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
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

auto bindToLuaRef(lua_State* l, ui::UIBinds& binds, std::string_view name) -> luabridge::LuaRef {
	const ui::BindValue* tree = binds.getValue(name);
	if (tree == nullptr) {
		return {l};
	}
	if (tree->isScalar()) {
		return variantToLuaRef(l, tree->value());
	}
	pushBindValue(l, *tree);
	auto ref = luabridge::LuaRef::fromStack(l, -1);
	lua_pop(l, 1);
	return ref;
}

auto assignBind(lua_State* l, ui::UIBinds& binds, const std::string& name, const luabridge::LuaRef& value) -> std::string {
	const ui::BindValue* current = binds.getValue(name);
	const bool wants_array = current != nullptr && current->isArray();
	const bool wants_object = current != nullptr && current->isObject();

	if (!wants_array && !wants_object && !value.isTable()) {
		binds.set(name, luaRefToVariant(l, value));
		return {};
	}

	value.push(l);
	BindKindHint hint = BindKindHint::any;
	if (wants_array) {
		hint = BindKindHint::array;
	} else if (wants_object) {
		hint = BindKindHint::object;
	}
	auto converted = luaToBindValue(l, -1, hint);
	lua_pop(l, 1);
	if (!converted) {
		return converted.error();
	}
	if (wants_array && !converted->isArray()) {
		return "expects an array (a list used by data-for)";
	}
	if (wants_object && !converted->isObject()) {
		return "expects an object (a table with string keys)";
	}
	if (!wants_array && !wants_object) {
		return "is a plain value in the document, it cannot hold a table (use data-for or {{name.field}} to make it one)";
	}
	binds.setValue(name, std::move(*converted));
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
	return bindToLuaRef(l, *binds, name);
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

	if (const std::string error = assignBind(l, *binds, name, value); !error.empty()) {
		luaL_error(l, "ui_binds: '%s' %s", name.c_str(), error.c_str());
	}
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
	return bindToLuaRef(l, *binds, name);
}

auto uiBindsSet(toast::Node* node, std::string_view name, const luabridge::LuaRef& value, lua_State* l) -> bool {
	ui::UIBinds* binds = resolveBinds(node);
	if (binds == nullptr || !binds->has(name)) {
		return false;
	}
	const std::string key(name);
	if (const std::string error = assignBind(l, *binds, key, value); !error.empty()) {
		luaL_error(l, "ui_binds: '%s' %s", key.c_str(), error.c_str());
	}
	return true;
}

}
