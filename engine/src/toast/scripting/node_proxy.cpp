#include "node_proxy.hpp"

#include "asset_proxy.hpp"
#include "lua_types.hpp"
#include "script_runtime.hpp"
#include "ui_binds_proxy.hpp"

#include <algorithm>
#include <format>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <toast/log.hpp>
#include <toast/reflect/reflect.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/world/node.hpp>
#include <utility>

namespace scripting {

namespace {

auto luaArgToAny(lua_State* l, const luabridge::LuaRef& v, std::string_view cpp_type, const char* param_name) -> std::any {
	const bool is_bool = cpp_type.contains("bool");
	const bool is_float = cpp_type.contains("float");
	const bool is_double = cpp_type.contains("double");
	const bool is_str = cpp_type.contains("string");
	const bool is_vec4 = cpp_type.contains("vec4");
	const bool is_vec3 = cpp_type.contains("vec3");
	const bool is_vec2 = cpp_type.contains("vec2");
	const bool is_quat = cpp_type.contains("quat");
	const bool is_box = cpp_type.contains("Box<");
	const bool is_asset = cpp_type.contains("AssetHandle<");
	// int must come last so it doesn't match "uint64_t" etc. before float/double
	const bool is_int = !is_float && !is_double && !is_bool &&
	                    (cpp_type.contains("int") || cpp_type.contains("long") || cpp_type.contains("short") || cpp_type == "char");

	if (is_bool) {
		if (!v.isBool() && !v.isNumber()) {
			luaL_error(l, "argument '%s': expected bool", param_name);
		}
		return v.isBool() ? v.unsafe_cast<bool>() : (v.unsafe_cast<lua_Number>() != 0.0);
	}
	if (is_float) {
		if (!v.isNumber()) {
			luaL_error(l, "argument '%s': expected number (float)", param_name);
		}
		return static_cast<float>(v.unsafe_cast<lua_Number>());
	}
	if (is_double) {
		if (!v.isNumber()) {
			luaL_error(l, "argument '%s': expected number (double)", param_name);
		}
		return v.unsafe_cast<lua_Number>();
	}
	if (is_str) {
		if (!v.isString()) {
			luaL_error(l, "argument '%s': expected string", param_name);
		}
		return v.tostring();
	}
	if (is_quat) {
		if (v.isInstance<glm::quat>()) {
			return v.unsafe_cast<glm::quat>();
		}
		if (v.isInstance<glm::vec4>()) {
			// accept vec4(x,y,z,w) for convenience
			const glm::vec4 vv = v.unsafe_cast<glm::vec4>();
			return glm::quat(vv.w, vv.x, vv.y, vv.z);
		}
		luaL_error(l, "argument '%s': expected quat", param_name);
	}
	if (is_vec4) {
		if (v.isInstance<Color4>()) {
			return v.unsafe_cast<Color4>().rgba;
		}
		if (!v.isInstance<glm::vec4>()) {
			luaL_error(l, "argument '%s': expected vec4", param_name);
		}
		return v.unsafe_cast<glm::vec4>();
	}
	if (is_vec3) {
		if (v.isInstance<Color3>()) {
			return v.unsafe_cast<Color3>().rgb;
		}
		if (!v.isInstance<glm::vec3>()) {
			luaL_error(l, "argument '%s': expected vec3", param_name);
		}
		return v.unsafe_cast<glm::vec3>();
	}
	if (is_vec2) {
		if (!v.isInstance<glm::vec2>()) {
			luaL_error(l, "argument '%s': expected vec2", param_name);
		}
		return v.unsafe_cast<glm::vec2>();
	}
	if (is_box) {
		if (!v.isInstance<NodeProxy>()) {
			luaL_error(l, "argument '%s': expected Node", param_name);
		}
		// Store as Box<Node>; the dynamic trampoline downcasts via .as<DerivedType>()
		const NodeProxy& np = v.unsafe_cast<NodeProxy>();
		if (!np.exists()) {
			luaL_error(l, "argument '%s': Node is dead", param_name);
		}
		return toast::Box<toast::Node>(np.box());
	}
	if (is_asset) {
		if (!v.isInstance<AssetProxy>()) {
			luaL_error(l, "argument '%s': expected Asset", param_name);
		}
		return v.unsafe_cast<AssetProxy>().uid();
	}
	if (is_int) {
		if (!v.isNumber()) {
			luaL_error(l, "argument '%s': expected int", param_name);
		}
		return static_cast<int>(v.unsafe_cast<lua_Integer>());
	}

	luaL_error(
	    l,
	    "argument '%s': unsupported C++ parameter type '%.*s' — cannot marshal from Lua",
	    param_name,
	    static_cast<int>(cpp_type.size()),
	    cpp_type.data()
	);
	return {};
}

// Builds a Lua array table when the any holds a std::vector of a supported type
auto anyVectorToLuaRef(lua_State* l, const std::any& value) -> luabridge::LuaRef;

auto anyReturnToLuaRef(lua_State* l, const std::any& val, std::string_view return_type) -> luabridge::LuaRef {
	using LuaRef = luabridge::LuaRef;
	if (return_type == "void" || !val.has_value()) {
		return {l};
	}
	if (const auto* v = std::any_cast<bool>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<int>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<int32_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<uint32_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<int64_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<uint64_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<int16_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<uint16_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<int8_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<uint8_t>(&val)) {
		return {l, static_cast<lua_Integer>(*v)};
	}
	if (const auto* v = std::any_cast<float>(&val)) {
		return {l, static_cast<lua_Number>(*v)};
	}
	if (const auto* v = std::any_cast<double>(&val)) {
		return {l, static_cast<lua_Number>(*v)};
	}
	if (const auto* v = std::any_cast<std::string>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<glm::vec2>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<glm::vec3>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<glm::vec4>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<glm::quat>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<Color3>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<Color4>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<toast::Box<toast::Node>>(&val)) {
		if (!v->exists()) {
			return {l};
		}
		return {l, NodeProxy(*v)};
	}
	if (const auto* v = std::any_cast<AssetProxy>(&val)) {
		return {l, *v};
	}
	if (const auto* v = std::any_cast<toast::UID>(&val)) {
		return {l, AssetProxy(*v)};
	}
	if (LuaRef r = anyVectorToLuaRef(l, val); !r.isNil()) {
		return r;
	}
	TOAST_WARN("Lua", "anyReturnToLuaRef: unhandled return type '{}', returning nil", return_type);
	return {l};
}

auto proxyMethodDispatch(lua_State* l) -> int;

// Strips the outer "Box<" ... ">" from a field.type string and returns the inner type name
auto innerBoxType(std::string_view type) -> std::string_view {
	constexpr std::string_view prefix = "Box<";
	if (!type.starts_with(prefix) || !type.ends_with('>')) {
		return {};
	}
	type.remove_prefix(prefix.size());
	type.remove_suffix(1);
	return type;
}

auto lookupField(const toast::NodeInfo* info, std::string_view key) -> const toast::FieldInfo* {
	return info->getField(key);
}

// Looks up a NodeInfo by short or fully-qualified name
auto lookupNodeInfo(std::string_view name) -> const toast::NodeInfo* {
	if (const auto* info = toast::NodeRegistry::reflect(name)) {
		return info;
	}
	std::string qualified = "toast::";
	qualified += name;
	return toast::NodeRegistry::reflect(qualified);
}

// Builds a Lua array table from std::vector<T> stored in a std::any
template<typename T>
auto pushVecTable(lua_State* l, const std::any& value) -> luabridge::LuaRef {
	const auto* vec = std::any_cast<std::vector<T>>(&value);
	if (!vec) {
		return {l};
	}
	lua_createtable(l, static_cast<int>(vec->size()), 0);
	for (size_t i = 0; i < vec->size(); ++i) {
		lua_pushinteger(l, static_cast<lua_Integer>(i + 1));
		if (auto r = luabridge::Stack<T>::push(l, (*vec)[i]); !r) {
			lua_pushnil(l);
		}
		lua_settable(l, -3);
	}
	return luabridge::LuaRef::fromStack(l);
}

// Rebuilds a std::vector<VecT> any as a table of ColorT
template<typename ColorT, typename VecT>
auto pushColorTable(lua_State* l, const std::any& value) -> luabridge::LuaRef {
	const auto* vec = std::any_cast<std::vector<VecT>>(&value);
	if (!vec) {
		return {l};
	}
	lua_createtable(l, static_cast<int>(vec->size()), 0);
	for (size_t i = 0; i < vec->size(); ++i) {
		lua_pushinteger(l, static_cast<lua_Integer>(i + 1));
		if (auto r = luabridge::Stack<ColorT>::push(l, ColorT((*vec)[i])); !r) {
			lua_pushnil(l);
		}
		lua_settable(l, -3);
	}
	return luabridge::LuaRef::fromStack(l);
}

auto anyVectorToLuaRef(lua_State* l, const std::any& value) -> luabridge::LuaRef {
	using luabridge::LuaRef;
	if (auto r = pushVecTable<bool>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<int>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<float>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<double>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<std::string>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<glm::vec2>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<glm::vec3>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<glm::vec4>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<glm::quat>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<Color3>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<Color4>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<AssetProxy>(l, value); !r.isNil()) {
		return r;
	}
	if (const auto* boxes = std::any_cast<std::vector<toast::Box<toast::Node>>>(&value)) {
		lua_createtable(l, static_cast<int>(boxes->size()), 0);
		for (size_t i = 0; i < boxes->size(); ++i) {
			lua_pushinteger(l, static_cast<lua_Integer>(i + 1));
			if ((*boxes)[i].exists()) {
				if (auto r = luabridge::Stack<NodeProxy>::push(l, NodeProxy((*boxes)[i])); !r) {
					lua_pushnil(l);
				}
			} else {
				lua_pushnil(l);
			}
			lua_settable(l, -3);
		}
		return LuaRef::fromStack(l);
	}
	if (const auto* uids = std::any_cast<std::vector<toast::UID>>(&value)) {
		lua_createtable(l, static_cast<int>(uids->size()), 0);
		for (size_t i = 0; i < uids->size(); ++i) {
			lua_pushinteger(l, static_cast<lua_Integer>(i + 1));
			if (auto r = luabridge::Stack<AssetProxy>::push(l, AssetProxy((*uids)[i])); !r) {
				lua_pushnil(l);
			}
			lua_settable(l, -3);
		}
		return LuaRef::fromStack(l);
	}
	return {l};
}

// Handles non-uid array fields for reads
auto nonUidArrayToLuaRef(lua_State* l, const std::any& value, std::string_view field_type) -> luabridge::LuaRef {
	if (field_type.contains("string")) {
		return pushVecTable<std::string>(l, value);
	}
	if (field_type.contains("vec4")) {
		return pushVecTable<glm::vec4>(l, value);
	}
	if (field_type.contains("vec3")) {
		return pushVecTable<glm::vec3>(l, value);
	}
	if (field_type.contains("vec2")) {
		return pushVecTable<glm::vec2>(l, value);
	}
	if (field_type.contains("quat")) {
		return pushVecTable<glm::quat>(l, value);
	}
	if (field_type.contains("double")) {
		return pushVecTable<double>(l, value);
	}
	if (field_type.contains("float")) {
		return pushVecTable<float>(l, value);
	}
	if (field_type.contains("bool")) {
		return pushVecTable<bool>(l, value);
	}
	// Integer fallback: try common integer types in order
	if (auto r = pushVecTable<int>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<uint32_t>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<int64_t>(l, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<uint64_t>(l, value); !r.isNil()) {
		return r;
	}
	return {l};
}

// Converts a Lua table to a std::vector stored in a std::any, dispatching on field.type
auto luaTableToAny(lua_State* l, const luabridge::LuaRef& table, const toast::FieldInfo& field, const std::string& key)
    -> std::any {
	const std::string_view t = field.type;
	if (t.contains("string")) {
		std::vector<std::string> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isString()) {
				luaL_error(l, "Field '%s': expected string elements", key.c_str());
			}
			v.push_back(it.value().tostring());
		}
		return v;
	}
	if (t.contains("vec4")) {
		std::vector<glm::vec4> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (it.value().isInstance<Color4>()) {
				v.push_back(it.value().unsafe_cast<Color4>().rgba);
			} else if (it.value().isInstance<glm::vec4>()) {
				v.push_back(it.value().unsafe_cast<glm::vec4>());
			} else {
				luaL_error(l, "Field '%s': expected vec4 or color4 elements", key.c_str());
			}
		}
		return v;
	}
	if (t.contains("vec3")) {
		std::vector<glm::vec3> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (it.value().isInstance<Color3>()) {
				v.push_back(it.value().unsafe_cast<Color3>().rgb);
			} else if (it.value().isInstance<glm::vec3>()) {
				v.push_back(it.value().unsafe_cast<glm::vec3>());
			} else {
				luaL_error(l, "Field '%s': expected vec3 or color3 elements", key.c_str());
			}
		}
		return v;
	}
	if (t.contains("vec2")) {
		std::vector<glm::vec2> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isInstance<glm::vec2>()) {
				luaL_error(l, "Field '%s': expected vec2 elements", key.c_str());
			}
			v.push_back(it.value().unsafe_cast<glm::vec2>());
		}
		return v;
	}
	if (t.contains("quat")) {
		std::vector<glm::quat> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (it.value().isInstance<glm::quat>()) {
				v.push_back(it.value().unsafe_cast<glm::quat>());
			} else if (it.value().isInstance<glm::vec4>()) {
				// accept vec4(x,y,z,w) for convenience
				const glm::vec4 vv = it.value().unsafe_cast<glm::vec4>();
				v.emplace_back(vv.w, vv.x, vv.y, vv.z);
			} else {
				luaL_error(l, "Field '%s': expected quat elements", key.c_str());
			}
		}
		return v;
	}
	if (t.contains("double")) {
		std::vector<double> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isNumber()) {
				luaL_error(l, "Field '%s': expected number elements", key.c_str());
			}
			v.push_back(it.value().unsafe_cast<lua_Number>());
		}
		return v;
	}
	if (t.contains("float")) {
		std::vector<float> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isNumber()) {
				luaL_error(l, "Field '%s': expected number elements", key.c_str());
			}
			v.push_back(static_cast<float>(it.value().unsafe_cast<lua_Number>()));
		}
		return v;
	}
	if (t.contains("bool")) {
		std::vector<bool> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isBool() && !it.value().isNumber()) {
				luaL_error(l, "Field '%s': expected bool elements", key.c_str());
			}
			v.push_back(it.value().isBool() ? it.value().unsafe_cast<bool>() : (it.value().unsafe_cast<lua_Number>() != 0.0));
		}
		return v;
	}
	// Integer fallback
	std::vector<int> v;
	for (luabridge::Iterator it(table); !it.isNil(); ++it) {
		if (!it.value().isNumber()) {
			luaL_error(l, "Field '%s': expected integer elements", key.c_str());
		}
		v.push_back(static_cast<int>(it.value().unsafe_cast<lua_Integer>()));
	}
	return v;
}

// Converts a std::any to a LuaRef
auto anyToLuaRef(lua_State* l, const std::any& value, const toast::FieldInfo& field) -> luabridge::LuaRef {
	using luabridge::LuaRef;
	using toast::FieldType;

	// dispatch on field.type since value_type defaults to int_t for unknown vectors
	if (field.is_array && field.value_type != FieldType::uid_t) {
		// [[Color]] vec arrays surface as color usertypes
		if (field.hasAttribute("Color")) {
			if (field.value_type == FieldType::vec3_t) {
				return pushColorTable<Color3, glm::vec3>(l, value);
			}
			if (field.value_type == FieldType::vec4_t) {
				return pushColorTable<Color4, glm::vec4>(l, value);
			}
		}
		return nonUidArrayToLuaRef(l, value, field.type);
	}

	switch (field.value_type) {
		case FieldType::bool_t:
			if (const auto* v = std::any_cast<bool>(&value)) {
				return {l, *v};
			}
			break;

		case FieldType::int_t:
			if (const auto* v = std::any_cast<int>(&value)) {
				return {l, *v};
			}
			if (const auto* v = std::any_cast<int32_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			if (const auto* v = std::any_cast<uint32_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			if (const auto* v = std::any_cast<int64_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			if (const auto* v = std::any_cast<uint64_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			if (const auto* v = std::any_cast<int16_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			if (const auto* v = std::any_cast<uint16_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			if (const auto* v = std::any_cast<int8_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			if (const auto* v = std::any_cast<uint8_t>(&value)) {
				return {l, static_cast<lua_Integer>(*v)};
			}
			break;

		case FieldType::float_t:
			if (const auto* v = std::any_cast<float>(&value)) {
				return {l, static_cast<lua_Number>(*v)};
			}
			break;

		case FieldType::double_t:
			if (const auto* v = std::any_cast<double>(&value)) {
				return {l, static_cast<lua_Number>(*v)};
			}
			break;

		case FieldType::string_t:
			if (const auto* v = std::any_cast<std::string>(&value)) {
				return {l, *v};
			}
			if (const auto* v = std::any_cast<std::string_view>(&value)) {
				return {l, std::string(*v)};
			}
			break;

		case FieldType::vec2_t:
			if (const auto* v = std::any_cast<glm::vec2>(&value)) {
				return {l, *v};
			}
			break;

		case FieldType::vec3_t:
			if (const auto* v = std::any_cast<glm::vec3>(&value)) {
				if (field.hasAttribute("Color")) {
					return {l, Color3(*v)};
				}
				return {l, *v};
			}
			break;

		case FieldType::vec4_t:
			if (const auto* v = std::any_cast<glm::vec4>(&value)) {
				if (field.hasAttribute("Color")) {
					return {l, Color4(*v)};
				}
				return {l, *v};
			}
			break;

		case FieldType::quaternion_t:
			if (const auto* v = std::any_cast<glm::quat>(&value)) {
				return {l, *v};
			}
			break;

		case FieldType::uid_t: {
			if (field.is_array) {
				if (const auto* uids = std::any_cast<std::vector<toast::UID>>(&value)) {
					lua_createtable(l, static_cast<int>(uids->size()), 0);
					for (size_t i = 0; i < uids->size(); ++i) {
						lua_pushinteger(l, static_cast<lua_Integer>(i + 1));
						auto result = luabridge::Stack<AssetProxy>::push(l, AssetProxy((*uids)[i]));
						if (!result) {
							TOAST_WARN("Scripting", "Failed to push AssetProxy to Lua: {}", result.message());
							lua_pushnil(l);
						}
						lua_settable(l, -3);
					}
					return luabridge::LuaRef::fromStack(l);
				}
				break;
			}
			// Box<Node>
			if (const auto* box = std::any_cast<toast::Box<toast::Node>>(&value)) {
				if (!box->exists()) {
					return {l};
				}
				return {l, NodeProxy(*box)};
			}
			// AssetHandle
			if (const auto* uid = std::any_cast<toast::UID>(&value)) {
				return {l, AssetProxy(*uid)};
			}
			break;
		}
	}

	TOAST_WARN("Scripting", "__index: unhandled field '{}' (FieldType {})", field.name, static_cast<int>(field.value_type));
	return {l};
}

// Converts an array-style Lua table to a std::vector<T> any
auto luaArrayTableToAny(lua_State* l, const luabridge::LuaRef& table) -> std::any {
	luabridge::LuaRef first = table[1];
	if (!first.isValid() || first.isNil()) {
		return {};
	}

	// element mismatch skips the conversion
	auto collect = [&]<typename T>(auto&& convert, auto&& matches) -> std::any {
		std::vector<T> out;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.key().isNumber() || !matches(it.value())) {
				return {};
			}
			out.push_back(convert(it.value()));
		}
		return out;
	};

	if (first.isBool()) {
		return collect.template operator()<bool>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<bool>(); }, [](const luabridge::LuaRef& e) { return e.isBool(); }
		);
	}
	if (first.isNumber()) {
		// All-integer tables rounds as ints, anything else widens to double
		std::vector<double> numbers;
		bool all_int = true;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.key().isNumber() || !it.value().isNumber()) {
				return {};
			}
			it.value().push(l);
			all_int = all_int && lua_isinteger(l, -1) != 0;
			lua_pop(l, 1);
			numbers.push_back(it.value().unsafe_cast<lua_Number>());
		}
		if (all_int) {
			std::vector<int> ints(numbers.size());
			std::ranges::transform(numbers, ints.begin(), [](double d) { return static_cast<int>(d); });
			return ints;
		}
		return numbers;
	}
	if (first.isString()) {
		return collect.template operator()<std::string>(
		    [](const luabridge::LuaRef& e) { return e.tostring(); }, [](const luabridge::LuaRef& e) { return e.isString(); }
		);
	}
	if (first.isInstance<glm::vec2>()) {
		return collect.template operator()<glm::vec2>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<glm::vec2>(); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<glm::vec2>(); }
		);
	}
	if (first.isInstance<glm::vec3>()) {
		return collect.template operator()<glm::vec3>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<glm::vec3>(); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<glm::vec3>(); }
		);
	}
	if (first.isInstance<glm::vec4>()) {
		return collect.template operator()<glm::vec4>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<glm::vec4>(); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<glm::vec4>(); }
		);
	}
	if (first.isInstance<glm::quat>()) {
		return collect.template operator()<glm::quat>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<glm::quat>(); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<glm::quat>(); }
		);
	}
	if (first.isInstance<Color3>()) {
		return collect.template operator()<Color3>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<Color3>(); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<Color3>(); }
		);
	}
	if (first.isInstance<Color4>()) {
		return collect.template operator()<Color4>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<Color4>(); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<Color4>(); }
		);
	}
	if (first.isInstance<NodeProxy>()) {
		return collect.template operator()<toast::Box<toast::Node>>(
		    [](const luabridge::LuaRef& e) { return toast::Box<toast::Node>(e.unsafe_cast<NodeProxy>().box()); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<NodeProxy>() && e.unsafe_cast<NodeProxy>().exists(); }
		);
	}
	if (first.isInstance<AssetProxy>()) {
		return collect.template operator()<AssetProxy>(
		    [](const luabridge::LuaRef& e) { return e.unsafe_cast<AssetProxy>(); },
		    [](const luabridge::LuaRef& e) { return e.isInstance<AssetProxy>(); }
		);
	}
	return {};
}

}

auto anyValueToLuaRef(lua_State* l, const std::any& value) -> luabridge::LuaRef {
	// anyReturnToLuaRef handles empty any -> nil, then dispatches on runtime type
	return anyReturnToLuaRef(l, value, "");
}

auto luaRefValueToAny(lua_State* l, const luabridge::LuaRef& v) -> std::any {
	if (!v.isValid() || v.isNil()) {
		return {};
	}
	if (v.isBool()) {
		return v.unsafe_cast<bool>();
	}
	if (v.isNumber()) {
		// Push temporarily so lua_isinteger can inspect the Lua subtype
		v.push(l);
		const bool is_int = lua_isinteger(l, -1) != 0;
		lua_pop(l, 1);
		if (is_int) {
			return static_cast<int>(v.unsafe_cast<lua_Integer>());
		}
		return v.unsafe_cast<lua_Number>();
	}
	if (v.isString()) {
		return v.tostring();
	}
	if (v.isInstance<glm::vec2>()) {
		return v.unsafe_cast<glm::vec2>();
	}
	if (v.isInstance<glm::vec3>()) {
		return v.unsafe_cast<glm::vec3>();
	}
	if (v.isInstance<glm::vec4>()) {
		return v.unsafe_cast<glm::vec4>();
	}
	if (v.isInstance<glm::quat>()) {
		return v.unsafe_cast<glm::quat>();
	}
	if (v.isInstance<Color3>()) {
		return v.unsafe_cast<Color3>();
	}
	if (v.isInstance<Color4>()) {
		return v.unsafe_cast<Color4>();
	}
	if (v.isInstance<NodeProxy>()) {
		const NodeProxy& np = v.unsafe_cast<NodeProxy>();
		if (np.exists()) {
			return toast::Box<toast::Node>(np.box());
		}
		return {};
	}
	if (v.isInstance<AssetProxy>()) {
		return v.unsafe_cast<AssetProxy>();
	}
	if (v.isTable()) {
		return luaArrayTableToAny(l, v);
	}
	return {};
}

NodeProxy::NodeProxy(toast::Box<toast::Node> box) noexcept : m_box(std::move(box)) { }

auto NodeProxy::exists() const noexcept -> bool {
	return m_box.exists();
}

auto NodeProxy::name() const -> std::string {
	if (!m_box.exists()) {
		return "";
	}
	return std::string(m_box->name());
}

auto NodeProxy::uid() const -> uint64_t {
	if (!m_box.exists()) {
		return 0;
	}
	return m_box->uid().data();
}

auto NodeProxy::find(const std::string& query, lua_State* l) -> luabridge::LuaRef {
	if (!m_box.exists()) {
		luaL_error(l, "NodeProxy::find: node reference is dead");
		return {l};
	}
	auto result = m_box->find(query);
	if (!result.exists()) {
		return {l};
	}
	return {l, NodeProxy(result)};
}

auto NodeProxy::search(const std::string& query) -> std::vector<NodeProxy> {
	if (!m_box.exists()) {
		return {};
	}
	auto boxes = m_box->search(query);
	std::vector<NodeProxy> out;
	out.reserve(boxes.size());
	for (auto& b : boxes) {
		out.emplace_back(b);
	}
	return out;
}

auto NodeProxy::create(const std::string& type, lua_State* l) -> luabridge::LuaRef {
	if (!m_box.exists()) {
		luaL_error(l, "NodeProxy::create: node reference is dead");
		return {l};
	}
	auto result = m_box->create(type);
	if (!result.exists()) {
		return {l};
	}
	return {l, NodeProxy(result)};
}

void NodeProxy::addDependsOn(const NodeProxy& other) {
	if (!m_box.exists() || !other.m_box.exists()) {
		return;
	}
	// first time every i actually had to use a const_cast
	m_box->addDependsOn(const_cast<toast::Node&>(*other.m_box));
}

auto NodeProxy::hasField(std::string_view key) const noexcept -> bool {
	if (!m_box.exists()) {
		return false;
	}
	const toast::NodeInfo* info = m_box->info();
	return info != nullptr && lookupField(info, key) != nullptr;
}

auto NodeProxy::call(const std::string& fn_name, lua_State* l) -> luabridge::LuaRef {
	int n_args = lua_gettop(l) - 2;
	int args_base = 3;

	luabridge::LuaRef result(l);
	int n_pushed = nodeProxyDispatchMethod(*this, fn_name, l, args_base, std::max(0, n_args));
	if (n_pushed > 0) {
		result = luabridge::LuaRef::fromStack(l, -1);
		lua_pop(l, n_pushed);
	}
	return result;
}

auto nodeProxyIndex(NodeProxy& proxy, const luabridge::LuaRef& key, lua_State* l) -> luabridge::LuaRef {
	if (!proxy.exists()) {
		luaL_error(l, "__index: node reference is dead");
		return {l};
	}

	if (!key.isString()) {
		return {l};
	}

	const std::string key_str = key.tostring();
	toast::Node* n = proxy.box().operator->();

	const toast::NodeInfo* info = n->info();
	if (!info) {
		return {l};
	}

	const toast::FieldInfo* f = lookupField(info, key_str);
	if (f) {
		std::any value = f->get(n);
		return anyToLuaRef(l, value, *f);
	}

	if (info->getMethod(key_str)) {
		lua_pushstring(l, key_str.c_str());
		lua_pushcclosure(l, proxyMethodDispatch, 1);
		return luabridge::LuaRef::fromStack(l);
	}

	if (key_str == "ui_binds") {
		return {l, UIBindsProxy(proxy.box())};
	}

	// self.<bind> sugar
	if (uiBindsHas(n, key_str)) {
		return uiBindsGet(n, key_str, l);
	}

	return {l};
}

auto nodeProxyDispatchMethod(NodeProxy& np, std::string_view name, lua_State* l, int args_base, int n_args) -> int {
	if (!np.exists()) {
		luaL_error(l, "method '%.*s': node reference is dead", static_cast<int>(name.size()), name.data());
		return 0;
	}

	toast::Node* n = np.box().operator->();
	const toast::NodeInfo* info = n->info();

	luabridge::LuaRef result(l);    // nil by default

	// Handle built-in NodeProxy methods by name
	if (name == "find") {
		const char* q = n_args >= 1 ? lua_tostring(l, args_base) : nullptr;
		if (!q) {
			luaL_error(l, "find: expected a query string");
			return 0;
		}
		result = np.find(q, l);
		result.push(l);
		return 1;
	}
	if (name == "search") {
		const char* q = n_args >= 1 ? lua_tostring(l, args_base) : nullptr;
		if (!q) {
			luaL_error(l, "search: expected a query string");
			return 0;
		}
		auto nodes = np.search(q);
		lua_createtable(l, static_cast<int>(nodes.size()), 0);
		for (size_t i = 0; i < nodes.size(); ++i) {
			lua_pushinteger(l, static_cast<lua_Integer>(i + 1));
			if (auto r = luabridge::Stack<NodeProxy>::push(l, nodes[i]); !r) {
				lua_pushnil(l);
			}
			lua_settable(l, -3);
		}
		return 1;
	}
	if (name == "create") {
		const char* type_name = n_args >= 1 ? lua_tostring(l, args_base) : "toast::Node";
		result = np.create(type_name ? type_name : "toast::Node", l);
		result.push(l);
		return 1;
	}
	if (name == "exists") {
		lua_pushboolean(l, np.exists() ? 1 : 0);
		return 1;
	}
	if (name == "name") {
		lua_pushstring(l, np.name().c_str());
		return 1;
	}
	if (name == "uid") {
		lua_pushinteger(l, static_cast<lua_Integer>(np.uid()));
		return 1;
	}
	if (name == "addDependsOn") {
		if (n_args < 1 || !lua_isuserdata(l, args_base)) {
			luaL_error(l, "addDependsOn: expected a Node argument");
			return 0;
		}
		auto other_result = luabridge::Stack<NodeProxy>::get(l, args_base);
		if (other_result) {
			np.addDependsOn(*other_result);
		}
		return 0;
	}
	if (name == "enabled") {
		if (n_args == 0) {
			lua_pushboolean(l, n->enabled() ? 1 : 0);
			return 1;
		}
		if (!lua_isboolean(l, args_base) && !lua_isnumber(l, args_base)) {
			luaL_error(l, "enabled: expected bool");
			return 0;
		}
		n->enabled(lua_toboolean(l, args_base) != 0);
		return 0;
	}
	if (name == "call") {
		if (n_args < 1 || lua_type(l, args_base) != LUA_TSTRING) {
			luaL_error(l, "call: expected a method name string as first argument");
			return 0;
		}
		const char* method_name = lua_tostring(l, args_base);
		return nodeProxyDispatchMethod(np, method_name, l, args_base + 1, n_args - 1);
	}

	// Reflected C++ method
	if (info) {
		if (const toast::FunctionInfo* fn = info->getMethod(name); fn && fn->invoke_dynamic) {
			// Validate arg count against required parameters
			const auto& params = fn->parameters;
			int required = 0;
			for (const auto& p : params) {
				if (!p.default_value.has_value()) {
					++required;
				}
			}
			if (n_args < required) {
				luaL_error(
				    l,
				    "method '%.*s': expected at least %d argument(s), got %d",
				    static_cast<int>(name.size()),
				    name.data(),
				    required,
				    n_args
				);
				return 0;
			}
			const int effective = std::min(n_args, static_cast<int>(params.size()));

			// Marshal Lua args to std::any
			std::vector<std::any> args;
			args.reserve(params.size());
			for (int i = 0; i < effective; ++i) {
				luabridge::LuaRef v = luabridge::LuaRef::fromStack(l, args_base + i);
				args.push_back(luaArgToAny(l, v, params[i].type, std::string(params[i].name).c_str()));
			}
			if (std::cmp_less(effective, params.size())) {
				// Can't fill default args dynamically without re-generating defaults as std::any
				// TODO: encode default_value strings to std::any in the generator for full support
				for (int i = effective; std::cmp_less(i, params.size()); ++i) {
					args.emplace_back();
				}
			}

			std::any ret = info->callAllDynamic(n, name, args);
			result = anyReturnToLuaRef(l, ret, fn->return_type);

			if (scripting::ScriptRuntime* rt = n->scriptRuntime()) {
				rt->callWithLuaStack(name, l, args_base, n_args);
			}
		}
	}

	result.push(l);
	return 1;
}

namespace {
auto proxyMethodDispatch(lua_State* l) -> int {
	const char* name = lua_tostring(l, lua_upvalueindex(1));
	auto np_result = luabridge::Stack<NodeProxy>::get(l, 1);
	if (!np_result) {
		luaL_error(l, "method '%s': invalid NodeProxy", name);
		return 0;
	}
	NodeProxy np = std::move(*np_result);
	// Stack: [NodeProxy_userdata(1), arg1(2), arg2(3), ...]
	int n_args = lua_gettop(l) - 1;
	int args_base = 2;
	return nodeProxyDispatchMethod(np, name, l, args_base, n_args);
}
}

auto nodeProxyNewindex(NodeProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* l)
    -> luabridge::LuaRef {
	if (!proxy.exists()) {
		luaL_error(l, "__newindex: node reference is dead");
		return {l};
	}

	if (!key.isString()) {
		luaL_error(l, "__newindex: field key must be a string");
		return {l};
	}

	const std::string key_str = key.tostring();
	toast::Node* n = proxy.box().operator->();

	const toast::NodeInfo* info = n->info();
	if (!info) {
		luaL_error(l, "__newindex: node has no type info");
		return {l};
	}

	const toast::FieldInfo* f = lookupField(info, key_str);
	if (!f) {
		// self.<bind> = value sugar
		if (uiBindsSet(n, key_str, value, l)) {
			return {l};
		}
		luaL_error(l, "__newindex: no reflected field '%s' on %s", key_str.c_str(), info->type.data());
		return {l};
	}

	using toast::FieldType;
	std::any any_value;

	// Non-uid arrays go through luaTableToAny
	// uid arrays are handled inside the uid_t case below since they need AssetProxy validation.
	if (f->is_array && f->value_type != FieldType::uid_t) {
		if (!value.isTable()) {
			luaL_error(l, "Field '%s' expects a table", key_str.c_str());
		}
		any_value = luaTableToAny(l, value, *f, key_str);
		f->set(n, std::move(any_value));
		return {l};
	}

	switch (f->value_type) {
		case FieldType::bool_t:
			if (!value.isBool() && !value.isNumber()) {
				luaL_error(l, "Field '%s' expects bool", key_str.c_str());
			}
			any_value = value.isBool() ? value.unsafe_cast<bool>() : (value.unsafe_cast<lua_Number>() != 0.0);
			break;

		case FieldType::int_t:
			if (!value.isNumber()) {
				luaL_error(l, "Field '%s' expects int", key_str.c_str());
			}
			any_value = static_cast<int>(value.unsafe_cast<lua_Integer>());
			break;

		case FieldType::float_t:
			if (!value.isNumber()) {
				luaL_error(l, "Field '%s' expects float", key_str.c_str());
			}
			any_value = static_cast<float>(value.unsafe_cast<lua_Number>());
			break;

		case FieldType::double_t:
			if (!value.isNumber()) {
				luaL_error(l, "Field '%s' expects double", key_str.c_str());
			}
			any_value = value.unsafe_cast<lua_Number>();
			break;

		case FieldType::string_t:
			if (!value.isString()) {
				luaL_error(l, "Field '%s' expects string", key_str.c_str());
			}
			any_value = value.tostring();
			break;

		case FieldType::vec2_t:
			if (!value.isInstance<glm::vec2>()) {
				luaL_error(l, "Field '%s' expects vec2", key_str.c_str());
			}
			any_value = value.unsafe_cast<glm::vec2>();
			break;

		case FieldType::vec3_t:
			if (value.isInstance<Color3>()) {
				any_value = value.unsafe_cast<Color3>().rgb;
			} else if (value.isInstance<glm::vec3>()) {
				any_value = value.unsafe_cast<glm::vec3>();
			} else {
				luaL_error(l, "Field '%s' expects vec3 or color3", key_str.c_str());
			}
			break;

		case FieldType::vec4_t:
			if (value.isInstance<Color4>()) {
				any_value = value.unsafe_cast<Color4>().rgba;
			} else if (value.isInstance<glm::vec4>()) {
				any_value = value.unsafe_cast<glm::vec4>();
			} else {
				luaL_error(l, "Field '%s' expects vec4 or color4", key_str.c_str());
			}
			break;

		case FieldType::quaternion_t:
			if (value.isInstance<glm::quat>()) {
				any_value = value.unsafe_cast<glm::quat>();
			} else if (value.isInstance<glm::vec4>()) {
				// accept vec4(x,y,z,w) for convenience
				const glm::vec4 v = value.unsafe_cast<glm::vec4>();
				any_value = glm::quat(v.w, v.x, v.y, v.z);
			} else {
				luaL_error(l, "Field '%s' expects quat", key_str.c_str());
			}
			break;

		case FieldType::uid_t: {
			const std::string_view type_str = f->type;

			if (!f->is_array && type_str.starts_with("Box<")) {
				if (!value.isInstance<NodeProxy>()) {
					luaL_error(l, "Field '%s' expects Node", key_str.c_str());
				}
				const NodeProxy& other = value.unsafe_cast<NodeProxy>();
				if (!other.exists()) {
					luaL_error(l, "Field '%s': assigned Node is dead", key_str.c_str());
				}
				// Validate derived type
				const std::string_view inner = innerBoxType(type_str);
				if (!inner.empty()) {
					const toast::NodeInfo* target = lookupNodeInfo(inner);
					if (target && !other.box()->info()->isA(target)) {
						luaL_error(
						    l,
						    "Field '%s': expected Box<%s>, got %s",
						    key_str.c_str(),
						    std::string(inner).c_str(),
						    other.box()->info() ? other.box()->info()->type.data() : "unknown"
						);
					}
				}
				// FieldAccess specialisation narrows back to Box<Derived>
				any_value = toast::Box<toast::Node>(other.box());

			} else if (!f->is_array && (type_str.starts_with("assets::AssetHandle<") || type_str.starts_with("AssetHandle<"))) {
				if (!value.isInstance<AssetProxy>()) {
					luaL_error(l, "Field '%s' expects Asset", key_str.c_str());
				}
				const AssetProxy& ap = value.unsafe_cast<AssetProxy>();
				if (auto err = ap.checkType(type_str); !err.empty()) {
					luaL_error(l, "Field '%s': %s", key_str.c_str(), err.c_str());
				}
				any_value = ap.uid();

			} else if (f->is_array) {
				if (!value.isTable()) {
					luaL_error(l, "Field '%s' expects a table of Asset handles", key_str.c_str());
				}
				std::vector<toast::UID> uids;
				for (luabridge::Iterator it(value); !it.isNil(); ++it) {
					if (it.value().isInstance<AssetProxy>()) {
						const AssetProxy& ap = it.value().unsafe_cast<AssetProxy>();
						if (auto err = ap.checkType(f->type); !err.empty()) {
							luaL_error(l, "Field '%s': %s", key_str.c_str(), err.c_str());
						}
						uids.push_back(ap.uid());
					}
				}
				any_value = std::move(uids);

			} else {
				luaL_error(l, "Field '%s': unsupported uid_t field type '%s'", key_str.c_str(), std::string(type_str).c_str());
			}
			break;
		}
	}

	f->set(n, std::move(any_value));
	return {l};
}

}
