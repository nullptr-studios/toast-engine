#include "ui_bind_convert.hpp"

#include <format>
#include <lua.hpp>
#include <utility>

namespace scripting {

namespace {

constexpr int k_max_depth = 8;

using Result = std::expected<ui::BindValue, std::string>;

auto where(const std::string& path) -> std::string {
	return path.empty() ? std::string("the value") : path;
}

auto scalarFromLua(lua_State* l, int index, const std::string& path) -> Result {
	switch (lua_type(l, index)) {
		case LUA_TNIL: return ui::BindValue::scalar(Rml::Variant());
		case LUA_TBOOLEAN: return ui::BindValue::scalar(Rml::Variant(lua_toboolean(l, index) != 0));
		case LUA_TNUMBER:
			if (lua_isinteger(l, index) != 0) {
				const lua_Integer v = lua_tointeger(l, index);
				if (v >= INT_MIN && v <= INT_MAX) {
					return ui::BindValue::scalar(Rml::Variant(static_cast<int>(v)));
				}
				return ui::BindValue::scalar(Rml::Variant(static_cast<double>(v)));
			}
			return ui::BindValue::scalar(Rml::Variant(static_cast<double>(lua_tonumber(l, index))));
		case LUA_TSTRING: {
			size_t length = 0;
			const char* text = lua_tolstring(l, index, &length);
			return ui::BindValue::scalar(Rml::Variant(Rml::String(text, length)));
		}
		default: return std::unexpected(std::format("unsupported {} value at {}", luaL_typename(l, index), where(path)));
	}
}

auto convert(lua_State* l, int index, BindKindHint hint, int depth, const std::string& path) -> Result {
	if (lua_type(l, index) != LUA_TTABLE) {
		return scalarFromLua(l, index, path);
	}
	if (depth > k_max_depth) {
		return std::unexpected(std::format("table nested deeper than {} levels (cyclic?) at {}", k_max_depth, where(path)));
	}

	index = lua_absindex(l, index);

	// classify the keys without running metamethods
	size_t count = 0;
	size_t integer_keys = 0;
	size_t string_keys = 0;
	lua_Integer max_index = 0;
	lua_pushnil(l);
	while (lua_next(l, index) != 0) {
		lua_pop(l, 1);    // keep the key for lua_next
		++count;
		if (lua_type(l, -1) == LUA_TSTRING) {
			++string_keys;
		} else if (lua_type(l, -1) == LUA_TNUMBER && lua_isinteger(l, -1) != 0 && lua_tointeger(l, -1) >= 1) {
			++integer_keys;
			max_index = std::max(max_index, lua_tointeger(l, -1));
		} else {
			lua_pop(l, 1);
			return std::unexpected(std::format("table key at {} must be a string or an integer from 1", where(path)));
		}
	}

	const bool is_array = count == 0 ? hint != BindKindHint::object : string_keys == 0;
	if (count > 0 && string_keys > 0 && integer_keys > 0) {
		return std::unexpected(std::format("mixed array/object table at {}", where(path)));
	}
	if (is_array && count > 0 && std::cmp_not_equal(max_index, integer_keys)) {
		return std::unexpected(std::format("sparse array at {}", where(path)));
	}

	if (is_array) {
		ui::BindValue out = ui::BindValue::array();
		out.items().reserve(count);
		for (lua_Integer i = 1; std::cmp_less_equal(i, count); ++i) {
			lua_rawgeti(l, index, i);
			auto child = convert(l, -1, BindKindHint::any, depth + 1, std::format("{}[{}]", path, i));
			lua_pop(l, 1);
			if (!child) {
				return child;
			}
			out.push(std::move(*child));
		}
		return out;
	}

	ui::BindValue out = ui::BindValue::object();
	lua_pushnil(l);
	while (lua_next(l, index) != 0) {
		// key at -2 and value at -1
		// the key stays a string because the conversion never touches it
		size_t length = 0;
		const char* key_text = lua_tolstring(l, -2, &length);
		const std::string key(key_text, length);
		auto child = convert(l, -1, BindKindHint::any, depth + 1, path.empty() ? key : std::format("{}.{}", path, key));
		if (!child) {
			lua_pop(l, 2);
			return child;
		}
		out.set(key, std::move(*child));
		lua_pop(l, 1);
	}
	return out;
}

void pushVariant(lua_State* l, const Rml::Variant& v) {
	switch (v.GetType()) {
		case Rml::Variant::BOOL: lua_pushboolean(l, v.Get<bool>() ? 1 : 0); break;
		case Rml::Variant::BYTE:
		case Rml::Variant::CHAR:
		case Rml::Variant::INT: lua_pushinteger(l, static_cast<lua_Integer>(v.Get<int>())); break;
		case Rml::Variant::INT64:
		case Rml::Variant::UINT:
		case Rml::Variant::UINT64: lua_pushinteger(l, static_cast<lua_Integer>(v.Get<int64_t>())); break;
		case Rml::Variant::FLOAT:
		case Rml::Variant::DOUBLE: lua_pushnumber(l, static_cast<lua_Number>(v.Get<double>())); break;
		case Rml::Variant::NONE: lua_pushnil(l); break;
		default: {
			const Rml::String text = v.Get<Rml::String>();
			lua_pushlstring(l, text.data(), text.size());
		}
	}
}

}

auto luaToBindValue(lua_State* l, int index, BindKindHint hint) -> std::expected<ui::BindValue, std::string> {
	return convert(l, index, hint, 1, {});
}

void pushBindValue(lua_State* l, const ui::BindValue& value) {
	if (value.isScalar()) {
		pushVariant(l, value.value());
		return;
	}
	luaL_checkstack(l, 4, "ui_binds");
	if (value.isArray()) {
		lua_createtable(l, static_cast<int>(value.items().size()), 0);
		lua_Integer i = 1;
		for (const auto& item : value.items()) {
			pushBindValue(l, item);
			lua_rawseti(l, -2, i++);
		}
		return;
	}
	lua_createtable(l, 0, static_cast<int>(value.members().size()));
	for (const auto& [name, member] : value.members()) {
		pushBindValue(l, member);
		lua_setfield(l, -2, name.c_str());
	}
}

}
