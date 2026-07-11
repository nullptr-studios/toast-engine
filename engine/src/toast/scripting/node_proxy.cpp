#include "node_proxy.hpp"

#include "asset_proxy.hpp"

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

luabridge::LuaRef NodeProxy::call(const std::string& /*fn_name*/, lua_State* L) {
	// TODO: invoke reflected/Lua function by name
	TOAST_WARN("Lua", "NodeProxy::call is not yet implemented");
	return luabridge::LuaRef(L);
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
	if (!f) {
		return luabridge::LuaRef(L);
	}

	std::any value = f->get(n);
	return anyToLuaRef(L, value, *f);
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
