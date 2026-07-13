#include "node_proxy.hpp"

#include "asset_proxy.hpp"
#include "script_runtime.hpp"

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

namespace scripting {

namespace {

std::any luaArgToAny(lua_State* L, const luabridge::LuaRef& v, std::string_view cpp_type, const char* param_name) {
	const bool is_bool = cpp_type.find("bool") != std::string_view::npos;
	const bool is_float = cpp_type.find("float") != std::string_view::npos;
	const bool is_double = cpp_type.find("double") != std::string_view::npos;
	const bool is_str = cpp_type.find("string") != std::string_view::npos;
	const bool is_vec4 = cpp_type.find("vec4") != std::string_view::npos;
	const bool is_vec3 = cpp_type.find("vec3") != std::string_view::npos;
	const bool is_vec2 = cpp_type.find("vec2") != std::string_view::npos;
	const bool is_quat = cpp_type.find("quat") != std::string_view::npos;
	const bool is_box = cpp_type.find("Box<") != std::string_view::npos;
	const bool is_asset = cpp_type.find("AssetHandle<") != std::string_view::npos;
	// int must come last so it doesn't match "uint64_t" etc. before float/double
	const bool is_int = !is_float && !is_double && !is_bool &&
	                    (cpp_type.find("int") != std::string_view::npos || cpp_type.find("long") != std::string_view::npos ||
	                     cpp_type.find("short") != std::string_view::npos || cpp_type == "char");

	if (is_bool) {
		if (!v.isBool() && !v.isNumber()) {
			luaL_error(L, "argument '%s': expected bool", param_name);
		}
		return v.isBool() ? v.unsafe_cast<bool>() : (v.unsafe_cast<lua_Number>() != 0.0);
	}
	if (is_float) {
		if (!v.isNumber()) {
			luaL_error(L, "argument '%s': expected number (float)", param_name);
		}
		return static_cast<float>(v.unsafe_cast<lua_Number>());
	}
	if (is_double) {
		if (!v.isNumber()) {
			luaL_error(L, "argument '%s': expected number (double)", param_name);
		}
		return v.unsafe_cast<lua_Number>();
	}
	if (is_str) {
		if (!v.isString()) {
			luaL_error(L, "argument '%s': expected string", param_name);
		}
		return v.tostring();
	}
	if (is_quat || is_vec4) {
		if (!v.isInstance<glm::vec4>()) {
			luaL_error(L, "argument '%s': expected vec4", param_name);
		}
		if (is_quat) {
			const glm::vec4 vv = v.unsafe_cast<glm::vec4>();
			return glm::quat(vv.w, vv.x, vv.y, vv.z);
		}
		return v.unsafe_cast<glm::vec4>();
	}
	if (is_vec3) {
		if (!v.isInstance<glm::vec3>()) {
			luaL_error(L, "argument '%s': expected vec3", param_name);
		}
		return v.unsafe_cast<glm::vec3>();
	}
	if (is_vec2) {
		if (!v.isInstance<glm::vec2>()) {
			luaL_error(L, "argument '%s': expected vec2", param_name);
		}
		return v.unsafe_cast<glm::vec2>();
	}
	if (is_box) {
		if (!v.isInstance<NodeProxy>()) {
			luaL_error(L, "argument '%s': expected Node", param_name);
		}
		// Store as Box<Node>; the dynamic trampoline downcasts via .as<DerivedType>()
		const NodeProxy& np = v.unsafe_cast<NodeProxy>();
		if (!np.exists()) {
			luaL_error(L, "argument '%s': Node is dead", param_name);
		}
		return toast::Box<toast::Node>(np.box());
	}
	if (is_asset) {
		if (!v.isInstance<AssetProxy>()) {
			luaL_error(L, "argument '%s': expected Asset", param_name);
		}
		return v.unsafe_cast<AssetProxy>().uid();
	}
	if (is_int) {
		if (!v.isNumber()) {
			luaL_error(L, "argument '%s': expected int", param_name);
		}
		return static_cast<int>(v.unsafe_cast<lua_Integer>());
	}

	luaL_error(
	    L,
	    "argument '%s': unsupported C++ parameter type '%.*s' — cannot marshal from Lua",
	    param_name,
	    static_cast<int>(cpp_type.size()),
	    cpp_type.data()
	);
	return {};
}

luabridge::LuaRef anyReturnToLuaRef(lua_State* L, const std::any& val, std::string_view return_type) {
	using LuaRef = luabridge::LuaRef;
	if (return_type == "void" || !val.has_value()) {
		return LuaRef(L);
	}
	if (auto* v = std::any_cast<bool>(&val)) {
		return LuaRef(L, *v);
	}
	if (auto* v = std::any_cast<int>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<int32_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<uint32_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<int64_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<uint64_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<int16_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<uint16_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<int8_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<uint8_t>(&val)) {
		return LuaRef(L, static_cast<lua_Integer>(*v));
	}
	if (auto* v = std::any_cast<float>(&val)) {
		return LuaRef(L, static_cast<lua_Number>(*v));
	}
	if (auto* v = std::any_cast<double>(&val)) {
		return LuaRef(L, static_cast<lua_Number>(*v));
	}
	if (auto* v = std::any_cast<std::string>(&val)) {
		return LuaRef(L, *v);
	}
	if (auto* v = std::any_cast<glm::vec2>(&val)) {
		return LuaRef(L, *v);
	}
	if (auto* v = std::any_cast<glm::vec3>(&val)) {
		return LuaRef(L, *v);
	}
	if (auto* v = std::any_cast<glm::vec4>(&val)) {
		return LuaRef(L, *v);
	}
	if (auto* v = std::any_cast<toast::Box<toast::Node>>(&val)) {
		if (!v->exists()) {
			return LuaRef(L);
		}
		return LuaRef(L, NodeProxy(*v));
	}
	TOAST_WARN("Scripting", "anyReturnToLuaRef: unhandled return type '{}' — returning nil", return_type);
	return LuaRef(L);
}

/int proxyMethodDispatch(lua_State* L);

// Strips the outer "Box<" ... ">" from a field.type string and returns the inner type name
std::string_view innerBoxType(std::string_view type) {
	constexpr std::string_view prefix = "Box<";
	if (!type.starts_with(prefix) || !type.ends_with('>')) {
		return {};
	}
	type.remove_prefix(prefix.size());
	type.remove_suffix(1);
	return type;
}

// Looks up a NodeInfo by short or fully-qualified name
const toast::NodeInfo* lookupNodeInfo(std::string_view name) {
	if (auto* info = toast::NodeRegistry::reflect(name)) {
		return info;
	}
	std::string qualified = "toast::";
	qualified += name;
	return toast::NodeRegistry::reflect(qualified);
}

// Builds a Lua array table from std::vector<T> stored in a std::any
template<typename T>
luabridge::LuaRef pushVecTable(lua_State* L, const std::any& value) {
	const auto* vec = std::any_cast<std::vector<T>>(&value);
	if (!vec) {
		return luabridge::LuaRef(L);
	}
	lua_createtable(L, static_cast<int>(vec->size()), 0);
	for (size_t i = 0; i < vec->size(); ++i) {
		lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
		if (auto r = luabridge::Stack<T>::push(L, (*vec)[i]); !r) {
			lua_pushnil(L);
		}
		lua_settable(L, -3);
	}
	return luabridge::LuaRef::fromStack(L, -1);
}

// Handles non-uid array fields for reads
luabridge::LuaRef nonUidArrayToLuaRef(lua_State* L, const std::any& value, std::string_view fieldType) {
	if (fieldType.find("string") != std::string_view::npos) {
		return pushVecTable<std::string>(L, value);
	}
	if (fieldType.find("vec4") != std::string_view::npos) {
		return pushVecTable<glm::vec4>(L, value);
	}
	if (fieldType.find("vec3") != std::string_view::npos) {
		return pushVecTable<glm::vec3>(L, value);
	}
	if (fieldType.find("vec2") != std::string_view::npos) {
		return pushVecTable<glm::vec2>(L, value);
	}
	if (fieldType.find("quat") != std::string_view::npos) {
		// Expose each quat as vec4(x,y,z,w) — glm::quat is not a registered Lua usertype.
		const auto* vec = std::any_cast<std::vector<glm::quat>>(&value);
		if (!vec) {
			return luabridge::LuaRef(L);
		}
		lua_createtable(L, static_cast<int>(vec->size()), 0);
		for (size_t i = 0; i < vec->size(); ++i) {
			lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
			const glm::quat& q = (*vec)[i];
			if (auto r = luabridge::Stack<glm::vec4>::push(L, glm::vec4(q.x, q.y, q.z, q.w)); !r) {
				lua_pushnil(L);
			}
			lua_settable(L, -3);
		}
		return luabridge::LuaRef::fromStack(L, -1);
	}
	if (fieldType.find("double") != std::string_view::npos) {
		return pushVecTable<double>(L, value);
	}
	if (fieldType.find("float") != std::string_view::npos) {
		return pushVecTable<float>(L, value);
	}
	if (fieldType.find("bool") != std::string_view::npos) {
		return pushVecTable<bool>(L, value);
	}
	// Integer fallback: try common integer types in order
	if (auto r = pushVecTable<int>(L, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<uint32_t>(L, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<int64_t>(L, value); !r.isNil()) {
		return r;
	}
	if (auto r = pushVecTable<uint64_t>(L, value); !r.isNil()) {
		return r;
	}
	return luabridge::LuaRef(L);
}

// Converts a Lua table to a std::vector stored in a std::any, dispatching on field.type
std::any luaTableToAny(lua_State* L, const luabridge::LuaRef& table, const toast::FieldInfo& field, const std::string& key) {
	const std::string_view t = field.type;
	if (t.find("string") != std::string_view::npos) {
		std::vector<std::string> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isString()) {
				luaL_error(L, "Field '%s': expected string elements", key.c_str());
			}
			v.push_back(it.value().tostring());
		}
		return v;
	}
	if (t.find("vec4") != std::string_view::npos) {
		std::vector<glm::vec4> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isInstance<glm::vec4>()) {
				luaL_error(L, "Field '%s': expected vec4 elements", key.c_str());
			}
			v.push_back(it.value().unsafe_cast<glm::vec4>());
		}
		return v;
	}
	if (t.find("vec3") != std::string_view::npos) {
		std::vector<glm::vec3> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isInstance<glm::vec3>()) {
				luaL_error(L, "Field '%s': expected vec3 elements", key.c_str());
			}
			v.push_back(it.value().unsafe_cast<glm::vec3>());
		}
		return v;
	}
	if (t.find("vec2") != std::string_view::npos) {
		std::vector<glm::vec2> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isInstance<glm::vec2>()) {
				luaL_error(L, "Field '%s': expected vec2 elements", key.c_str());
			}
			v.push_back(it.value().unsafe_cast<glm::vec2>());
		}
		return v;
	}
	if (t.find("quat") != std::string_view::npos) {
		// Accept vec4(x,y,z,w)
		std::vector<glm::quat> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isInstance<glm::vec4>()) {
				luaL_error(L, "Field '%s': expected vec4(x,y,z,w) elements for quaternion", key.c_str());
			}
			const glm::vec4 vv = it.value().unsafe_cast<glm::vec4>();
			v.emplace_back(vv.w, vv.x, vv.y, vv.z);
		}
		return v;
	}
	if (t.find("double") != std::string_view::npos) {
		std::vector<double> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isNumber()) {
				luaL_error(L, "Field '%s': expected number elements", key.c_str());
			}
			v.push_back(it.value().unsafe_cast<lua_Number>());
		}
		return v;
	}
	if (t.find("float") != std::string_view::npos) {
		std::vector<float> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isNumber()) {
				luaL_error(L, "Field '%s': expected number elements", key.c_str());
			}
			v.push_back(static_cast<float>(it.value().unsafe_cast<lua_Number>()));
		}
		return v;
	}
	if (t.find("bool") != std::string_view::npos) {
		std::vector<bool> v;
		for (luabridge::Iterator it(table); !it.isNil(); ++it) {
			if (!it.value().isBool() && !it.value().isNumber()) {
				luaL_error(L, "Field '%s': expected bool elements", key.c_str());
			}
			v.push_back(it.value().isBool() ? it.value().unsafe_cast<bool>() : (it.value().unsafe_cast<lua_Number>() != 0.0));
		}
		return v;
	}
	// Integer fallback
	std::vector<int> v;
	for (luabridge::Iterator it(table); !it.isNil(); ++it) {
		if (!it.value().isNumber()) {
			luaL_error(L, "Field '%s': expected integer elements", key.c_str());
		}
		v.push_back(static_cast<int>(it.value().unsafe_cast<lua_Integer>()));
	}
	return v;
}

// Converts a std::any to a LuaRef
luabridge::LuaRef anyToLuaRef(lua_State* L, const std::any& value, const toast::FieldInfo& field) {
	using luabridge::LuaRef;
	using toast::FieldType;

	// dispatch on field.type since value_type defaults to int_t for unknown vectors
	if (field.is_array && field.value_type != FieldType::uid_t) {
		return nonUidArrayToLuaRef(L, value, field.type);
	}

	switch (field.value_type) {
		case FieldType::bool_t:
			if (auto* v = std::any_cast<bool>(&value)) {
				return LuaRef(L, *v);
			}
			break;

		case FieldType::int_t:
			if (auto* v = std::any_cast<int>(&value)) {
				return LuaRef(L, *v);
			}
			if (auto* v = std::any_cast<int32_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			if (auto* v = std::any_cast<uint32_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			if (auto* v = std::any_cast<int64_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			if (auto* v = std::any_cast<uint64_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			if (auto* v = std::any_cast<int16_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			if (auto* v = std::any_cast<uint16_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			if (auto* v = std::any_cast<int8_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			if (auto* v = std::any_cast<uint8_t>(&value)) {
				return LuaRef(L, static_cast<lua_Integer>(*v));
			}
			break;

		case FieldType::float_t:
			if (auto* v = std::any_cast<float>(&value)) {
				return LuaRef(L, static_cast<lua_Number>(*v));
			}
			break;

		case FieldType::double_t:
			if (auto* v = std::any_cast<double>(&value)) {
				return LuaRef(L, static_cast<lua_Number>(*v));
			}
			break;

		case FieldType::string_t:
			if (auto* v = std::any_cast<std::string>(&value)) {
				return LuaRef(L, *v);
			}
			if (auto* v = std::any_cast<std::string_view>(&value)) {
				return LuaRef(L, std::string(*v));
			}
			break;

		case FieldType::vec2_t:
			if (auto* v = std::any_cast<glm::vec2>(&value)) {
				return LuaRef(L, *v);
			}
			break;

		case FieldType::vec3_t:
			if (auto* v = std::any_cast<glm::vec3>(&value)) {
				return LuaRef(L, *v);
			}
			break;

		case FieldType::vec4_t:
			if (auto* v = std::any_cast<glm::vec4>(&value)) {
				return LuaRef(L, *v);
			}
			break;

		case FieldType::quaternion_t:
			// glm::quat is not a registered Lua usertype; expose as vec4(x,y,z,w)
			if (auto* v = std::any_cast<glm::quat>(&value)) {
				return LuaRef(L, glm::vec4(v->x, v->y, v->z, v->w));
			}
			break;

		case FieldType::uid_t: {
			if (field.is_array) {
				if (auto* uids = std::any_cast<std::vector<toast::UID>>(&value)) {
					lua_createtable(L, static_cast<int>(uids->size()), 0);
					for (size_t i = 0; i < uids->size(); ++i) {
						lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
						auto result = luabridge::Stack<AssetProxy>::push(L, AssetProxy((*uids)[i]));
						if (!result) {
							TOAST_WARN("Scripting", "Failed to push AssetProxy to Lua: {}", result.message());
							lua_pushnil(L);
						}
						lua_settable(L, -3);
					}
					return luabridge::LuaRef::fromStack(L, -1);
				}
				break;
			}
			// Box<Node>
			if (auto* box = std::any_cast<toast::Box<toast::Node>>(&value)) {
				if (!box->exists()) {
					return LuaRef(L);
				}
				return LuaRef(L, NodeProxy(*box));
			}
			// AssetHandle
			if (auto* uid = std::any_cast<toast::UID>(&value)) {
				return LuaRef(L, AssetProxy(*uid));
			}
			break;
		}
	}

	TOAST_WARN("Scripting", "__index: unhandled field '{}' (FieldType {})", field.name, static_cast<int>(field.value_type));
	return luabridge::LuaRef(L);
}

}

luabridge::LuaRef anyValueToLuaRef(lua_State* L, const std::any& value) {
	// anyReturnToLuaRef handles empty any -> nil, then dispatches on runtime type
	return anyReturnToLuaRef(L, value, "");
}

std::any luaRefValueToAny(lua_State* L, const luabridge::LuaRef& v) {
	if (v.isNil() || v.isNone()) {
		return {};
	}
	if (v.isBool()) {
		return v.unsafe_cast<bool>();
	}
	if (v.isNumber()) {
		// Push temporarily so lua_isinteger can inspect the Lua subtype
		v.push(L);
		const bool is_int = lua_isinteger(L, -1) != 0;
		lua_pop(L, 1);
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
	if (v.isInstance<NodeProxy>()) {
		const NodeProxy& np = v.unsafe_cast<NodeProxy>();
		if (np.exists()) {
			return toast::Box<toast::Node>(np.box());
		}
		return {};
	}
	return {};
}

NodeProxy::NodeProxy(toast::Box<toast::Node> box) noexcept : m_box(std::move(box)) { }

bool NodeProxy::exists() const noexcept {
	return m_box.exists();
}

std::string NodeProxy::name() const {
	if (!m_box.exists()) {
		return "";
	}
	return std::string(m_box->name());
}

uint64_t NodeProxy::uid() const {
	if (!m_box.exists()) {
		return 0;
	}
	return m_box->uid().data();
}

luabridge::LuaRef NodeProxy::find(const std::string& query, lua_State* L) {
	if (!m_box.exists()) {
		luaL_error(L, "NodeProxy::find: node reference is dead");
		return luabridge::LuaRef(L);
	}
	auto result = m_box->find(query);
	if (!result.exists()) {
		return luabridge::LuaRef(L);
	}
	return luabridge::LuaRef(L, NodeProxy(result));
}

std::vector<NodeProxy> NodeProxy::search(const std::string& query) {
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

luabridge::LuaRef NodeProxy::create(const std::string& type, lua_State* L) {
	if (!m_box.exists()) {
		luaL_error(L, "NodeProxy::create: node reference is dead");
		return luabridge::LuaRef(L);
	}
	auto result = m_box->create(type);
	if (!result.exists()) {
		return luabridge::LuaRef(L);
	}
	return luabridge::LuaRef(L, NodeProxy(result));
}

void NodeProxy::addDependsOn(const NodeProxy& other) {
	if (!m_box.exists() || !other.m_box.exists()) {
		return;
	}
	// first time every i actually had to use a const_cast
	m_box->addDependsOn(const_cast<toast::Node&>(*other.m_box));
}

bool NodeProxy::hasField(std::string_view key) const noexcept {
	if (!m_box.exists()) {
		return false;
	}
	const toast::NodeInfo* info = m_box->info();
	return info != nullptr && info->getField(key) != nullptr;
}

luabridge::LuaRef NodeProxy::call(const std::string& fn_name, lua_State* L) {
	int n_args = lua_gettop(L) - 2;
	int args_base = 3;

	luabridge::LuaRef result(L);
	int n_pushed = nodeProxyDispatchMethod(*this, fn_name, L, args_base, std::max(0, n_args));
	if (n_pushed > 0) {
		result = luabridge::LuaRef::fromStack(L, -1);
		lua_pop(L, n_pushed);
	}
	return result;
}

luabridge::LuaRef nodeProxyIndex(NodeProxy& proxy, const luabridge::LuaRef& key, lua_State* L) {
	if (!proxy.exists()) {
		luaL_error(L, "__index: node reference is dead");
		return luabridge::LuaRef(L);
	}

	if (!key.isString()) {
		return luabridge::LuaRef(L);
	}

	const std::string key_str = key.tostring();
	toast::Node* n = proxy.box().operator->();

	const toast::NodeInfo* info = n->info();
	if (!info) {
		return luabridge::LuaRef(L);
	}

	const toast::FieldInfo* f = info->getField(key_str);
	if (f) {
		std::any value = f->get(n);
		return anyToLuaRef(L, value, *f);
	}

	if (info->getMethod(key_str)) {
		lua_pushstring(L, key_str.c_str());
		lua_pushcclosure(L, proxyMethodDispatch, 1);
		return luabridge::LuaRef::fromStack(L, -1);
	}

	return luabridge::LuaRef(L);
}

int nodeProxyDispatchMethod(NodeProxy& np, std::string_view name, lua_State* L, int args_base, int n_args) {
	if (!np.exists()) {
		luaL_error(L, "method '%.*s': node reference is dead", static_cast<int>(name.size()), name.data());
		return 0;
	}

	toast::Node* n = np.box().operator->();
	const toast::NodeInfo* info = n->info();

	luabridge::LuaRef result(L);    // nil by default

	// Handle built-in NodeProxy methods by name
	if (name == "find") {
		const char* q = n_args >= 1 ? lua_tostring(L, args_base) : nullptr;
		if (!q) {
			luaL_error(L, "find: expected a query string");
			return 0;
		}
		result = np.find(q, L);
		result.push(L);
		return 1;
	}
	if (name == "search") {
		const char* q = n_args >= 1 ? lua_tostring(L, args_base) : nullptr;
		if (!q) {
			luaL_error(L, "search: expected a query string");
			return 0;
		}
		auto nodes = np.search(q);
		lua_createtable(L, static_cast<int>(nodes.size()), 0);
		for (size_t i = 0; i < nodes.size(); ++i) {
			lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
			if (auto r = luabridge::Stack<NodeProxy>::push(L, nodes[i]); !r) {
				lua_pushnil(L);
			}
			lua_settable(L, -3);
		}
		return 1;
	}
	if (name == "create") {
		const char* type_name = n_args >= 1 ? lua_tostring(L, args_base) : "toast::Node";
		result = np.create(type_name ? type_name : "toast::Node", L);
		result.push(L);
		return 1;
	}
	if (name == "exists") {
		lua_pushboolean(L, np.exists() ? 1 : 0);
		return 1;
	}
	if (name == "name") {
		lua_pushstring(L, np.name().c_str());
		return 1;
	}
	if (name == "uid") {
		lua_pushinteger(L, static_cast<lua_Integer>(np.uid()));
		return 1;
	}
	if (name == "addDependsOn") {
		if (n_args < 1 || !lua_isuserdata(L, args_base)) {
			luaL_error(L, "addDependsOn: expected a Node argument");
			return 0;
		}
		auto other_result = luabridge::Stack<NodeProxy>::get(L, args_base);
		if (other_result) {
			np.addDependsOn(*other_result);
		}
		return 0;
	}
	if (name == "enabled") {
		if (n_args == 0) {
			lua_pushboolean(L, n->enabled() ? 1 : 0);
			return 1;
		}
		if (!lua_isboolean(L, args_base) && !lua_isnumber(L, args_base)) {
			luaL_error(L, "enabled: expected bool");
			return 0;
		}
		n->enabled(lua_toboolean(L, args_base) != 0);
		return 0;
	}
	if (name == "call") {
		if (n_args < 1 || lua_type(L, args_base) != LUA_TSTRING) {
			luaL_error(L, "call: expected a method name string as first argument");
			return 0;
		}
		const char* method_name = lua_tostring(L, args_base);
		return nodeProxyDispatchMethod(np, method_name, L, args_base + 1, n_args - 1);
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
				    L,
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
				luabridge::LuaRef v = luabridge::LuaRef::fromStack(L, args_base + i);
				args.push_back(luaArgToAny(L, v, params[i].type, std::string(params[i].name).c_str()));
			}
			if (effective < static_cast<int>(params.size())) {
				// Can't fill default args dynamically without re-generating defaults as std::any
				// TODO: encode default_value strings to std::any in the generator for full support
				for (int i = effective; i < static_cast<int>(params.size()); ++i) {
					args.emplace_back();
				}
			}

			std::any ret = info->callAllDynamic(n, name, args);
			result = anyReturnToLuaRef(L, ret, fn->return_type);

			if (scripting::ScriptRuntime* rt = n->scriptRuntime()) {
				rt->callWithLuaStack(name, L, args_base, n_args);
			}
		}
	}

	result.push(L);
	return 1;
}

namespace {
int proxyMethodDispatch(lua_State* L) {
	const char* name = lua_tostring(L, lua_upvalueindex(1));
	auto np_result = luabridge::Stack<NodeProxy>::get(L, 1);
	if (!np_result) {
		luaL_error(L, "method '%s': invalid NodeProxy", name);
		return 0;
	}
	NodeProxy np = std::move(*np_result);
	// Stack: [NodeProxy_userdata(1), arg1(2), arg2(3), ...]
	int n_args = lua_gettop(L) - 1;
	int args_base = 2;
	return nodeProxyDispatchMethod(np, name, L, args_base, n_args);
}
}

luabridge::LuaRef
    nodeProxyNewindex(NodeProxy& proxy, const luabridge::LuaRef& key, const luabridge::LuaRef& value, lua_State* L) {
	if (!proxy.exists()) {
		luaL_error(L, "__newindex: node reference is dead");
		return luabridge::LuaRef(L);
	}

	if (!key.isString()) {
		luaL_error(L, "__newindex: field key must be a string");
		return luabridge::LuaRef(L);
	}

	const std::string key_str = key.tostring();
	toast::Node* n = proxy.box().operator->();

	const toast::NodeInfo* info = n->info();
	if (!info) {
		luaL_error(L, "__newindex: node has no type info");
		return luabridge::LuaRef(L);
	}

	const toast::FieldInfo* f = info->getField(key_str);
	if (!f) {
		luaL_error(L, "__newindex: no reflected field '%s' on %s", key_str.c_str(), info->type.data());
		return luabridge::LuaRef(L);
	}

	using toast::FieldType;
	std::any any_value;

	// Non-uid arrays go through luaTableToAny
	// uid arrays are handled inside the uid_t case below since they need AssetProxy validation.
	if (f->is_array && f->value_type != FieldType::uid_t) {
		if (!value.isTable()) {
			luaL_error(L, "Field '%s' expects a table", key_str.c_str());
		}
		any_value = luaTableToAny(L, value, *f, key_str);
		f->set(n, std::move(any_value));
		return luabridge::LuaRef(L);
	}

	switch (f->value_type) {
		case FieldType::bool_t:
			if (!value.isBool() && !value.isNumber()) {
				luaL_error(L, "Field '%s' expects bool", key_str.c_str());
			}
			any_value = value.isBool() ? value.unsafe_cast<bool>() : (value.unsafe_cast<lua_Number>() != 0.0);
			break;

		case FieldType::int_t:
			if (!value.isNumber()) {
				luaL_error(L, "Field '%s' expects int", key_str.c_str());
			}
			any_value = static_cast<int>(value.unsafe_cast<lua_Integer>());
			break;

		case FieldType::float_t:
			if (!value.isNumber()) {
				luaL_error(L, "Field '%s' expects float", key_str.c_str());
			}
			any_value = static_cast<float>(value.unsafe_cast<lua_Number>());
			break;

		case FieldType::double_t:
			if (!value.isNumber()) {
				luaL_error(L, "Field '%s' expects double", key_str.c_str());
			}
			any_value = value.unsafe_cast<lua_Number>();
			break;

		case FieldType::string_t:
			if (!value.isString()) {
				luaL_error(L, "Field '%s' expects string", key_str.c_str());
			}
			any_value = value.tostring();
			break;

		case FieldType::vec2_t:
			if (!value.isInstance<glm::vec2>()) {
				luaL_error(L, "Field '%s' expects vec2", key_str.c_str());
			}
			any_value = value.unsafe_cast<glm::vec2>();
			break;

		case FieldType::vec3_t:
			if (!value.isInstance<glm::vec3>()) {
				luaL_error(L, "Field '%s' expects vec3", key_str.c_str());
			}
			any_value = value.unsafe_cast<glm::vec3>();
			break;

		case FieldType::vec4_t:
			if (!value.isInstance<glm::vec4>()) {
				luaL_error(L, "Field '%s' expects vec4", key_str.c_str());
			}
			any_value = value.unsafe_cast<glm::vec4>();
			break;

		case FieldType::quaternion_t:
			// Accept vec4(x,y,z,w) as the Lua representation of a quaternion
			if (!value.isInstance<glm::vec4>()) {
				luaL_error(L, "Field '%s' expects vec4(x,y,z,w) for quaternion", key_str.c_str());
			}
			{
				const glm::vec4 v = value.unsafe_cast<glm::vec4>();
				any_value = glm::quat(v.w, v.x, v.y, v.z);
			}
			break;

		case FieldType::uid_t: {
			const std::string_view type_str = f->type;

			if (!f->is_array && type_str.starts_with("Box<")) {
				if (!value.isInstance<NodeProxy>()) {
					luaL_error(L, "Field '%s' expects Node", key_str.c_str());
				}
				const NodeProxy& other = value.unsafe_cast<NodeProxy>();
				if (!other.exists()) {
					luaL_error(L, "Field '%s': assigned Node is dead", key_str.c_str());
				}
				// Validate derived type
				const std::string_view inner = innerBoxType(type_str);
				if (!inner.empty()) {
					const toast::NodeInfo* target = lookupNodeInfo(inner);
					if (target && !other.box()->info()->isA(target)) {
						luaL_error(
						    L,
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
					luaL_error(L, "Field '%s' expects Asset", key_str.c_str());
				}
				const AssetProxy& ap = value.unsafe_cast<AssetProxy>();
				if (auto err = ap.checkType(type_str); !err.empty()) {
					luaL_error(L, "Field '%s': %s", key_str.c_str(), err.c_str());
				}
				any_value = ap.uid();

			} else if (f->is_array) {
				if (!value.isTable()) {
					luaL_error(L, "Field '%s' expects a table of Asset handles", key_str.c_str());
				}
				std::vector<toast::UID> uids;
				for (luabridge::Iterator it(value); !it.isNil(); ++it) {
					if (it.value().isInstance<AssetProxy>()) {
						const AssetProxy& ap = it.value().unsafe_cast<AssetProxy>();
						if (auto err = ap.checkType(f->type); !err.empty()) {
							luaL_error(L, "Field '%s': %s", key_str.c_str(), err.c_str());
						}
						uids.push_back(ap.uid());
					}
				}
				any_value = std::move(uids);

			} else {
				luaL_error(L, "Field '%s': unsupported uid_t field type '%s'", key_str.c_str(), std::string(type_str).c_str());
			}
			break;
		}
	}

	f->set(n, std::move(any_value));
	return luabridge::LuaRef(L);
}

}
