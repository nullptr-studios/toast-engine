#include "script_runtime.hpp"

#include "asset_proxy.hpp"
#include "lua_state.hpp"
#include "lua_types.hpp"
#include "lua_util.hpp"
#include "node_proxy.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <format>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <toast/log.hpp>
#include <toast/world/node.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <utility>

namespace scripting {

namespace {

auto isCallableProxyKey(std::string_view key, const toast::NodeInfo* info) noexcept -> bool {
	// Builtin methods
	if (key == "find" || key == "search" || key == "create" || key == "exists" || key == "name" || key == "uid" ||
	    key == "addDependsOn" || key == "call" || key == "enabled") {
		return true;
	}
	return info != nullptr && info->getMethod(key) != nullptr;
}

auto selfMethodDispatch(lua_State* l) -> int {
	const char* name = lua_tostring(l, lua_upvalueindex(1));
	auto* np = static_cast<NodeProxy*>(lua_touserdata(l, lua_upvalueindex(2)));
	if (!np || !name) {
		if (name) {
			luaL_error(l, "method '%s': no NodeProxy available", name);
		}
		return 0;
	}
	const int n_args = lua_gettop(l) - 1;    // exclude self at slot 1
	const int args_base = 2;
	return nodeProxyDispatchMethod(*np, name, l, args_base, n_args);
}

auto phaseToLuaName(toast::TickFunctionList phase) noexcept -> const char* {
	using F = toast::TickFunctionList;
	switch (phase) {
		case F::load: return "load";
		case F::save: return "save";
		case F::pre_init: return "pre_init";
		case F::init: return "init";
		case F::destroy: return "destroy";
		case F::begin: return "begin";
		case F::end: return "_end";    // "end" is a reserved Lua keyword
		case F::on_enable: return "onEnable";
		case F::on_disable: return "onDisable";
		case F::early_tick: return "earlyTick";
		case F::tick: return "tick";
		case F::post_physics: return "postPhysics";
		case F::late_tick: return "lateTick";
		default: return nullptr;
	}
}

auto isExportableKey(const luabridge::LuaRef& key) -> bool {
	return key.isString() && !key.tostring().starts_with('_');
}

auto classifyLeaf(lua_State* l, const luabridge::LuaRef& v) -> std::optional<LuaVarDesc> {
	LuaVarDesc d;
	if (v.isBool()) {
		d.kind = LuaVarKind::boolean;
		return d;
	}
	if (v.isNumber()) {
		v.push(l);
		const bool is_int = lua_isinteger(l, -1) != 0;
		lua_pop(l, 1);
		d.kind = is_int ? LuaVarKind::integer : LuaVarKind::number;
		return d;
	}
	if (v.isString()) {
		d.kind = LuaVarKind::string;
		return d;
	}
	if (v.isInstance<glm::vec2>()) {
		d.kind = LuaVarKind::vec2;
		return d;
	}
	if (v.isInstance<glm::vec3>()) {
		d.kind = LuaVarKind::vec3;
		return d;
	}
	if (v.isInstance<glm::vec4>()) {
		d.kind = LuaVarKind::vec4;
		return d;
	}
	if (v.isInstance<Color3>()) {
		d.kind = LuaVarKind::color3;
		return d;
	}
	if (v.isInstance<Color4>()) {
		d.kind = LuaVarKind::color4;
		return d;
	}
	if (v.isInstance<TypeMarker>()) {
		const TypeMarker& marker = v.unsafe_cast<TypeMarker>();
		d.kind = marker.kind == TypeMarker::Kind::node ? LuaVarKind::node_ref : LuaVarKind::asset_ref;
		d.ref_type = marker.type_name;
		return d;
	}
	if (v.isInstance<NodeProxy>()) {
		const NodeProxy& np = v.unsafe_cast<NodeProxy>();
		d.kind = LuaVarKind::node_ref;
		if (np.exists() && np.box()->info() != nullptr) {
			d.ref_type = np.box()->info()->type;
		}
		return d;
	}
	if (v.isInstance<AssetProxy>()) {
		d.kind = LuaVarKind::asset_ref;
		d.ref_type = v.unsafe_cast<AssetProxy>().type();
		return d;
	}
	return std::nullopt;
}

auto declPos(std::string_view src, std::string_view key, size_t from) -> size_t {
	size_t pos = from;
	while ((pos = src.find(key, pos)) != std::string_view::npos) {
		const char before = pos > 0 ? src[pos - 1] : ' ';
		const bool boundary = std::isalnum(static_cast<unsigned char>(before)) == 0 && before != '_';
		size_t after = pos + key.size();
		while (after < src.size() && std::isspace(static_cast<unsigned char>(src[after])) != 0) {
			++after;
		}
		const bool assigned = after < src.size() && src[after] == '=' && (after + 1 >= src.size() || src[after + 1] != '=');
		if (boundary && assigned) {
			return pos;
		}
		pos += key.size();
	}
	return std::string_view::npos;
}

template<typename T>
void sortByDeclaration(std::vector<T>& entries, std::string_view src, size_t from) {
	std::ranges::stable_sort(entries, [&](const T& a, const T& b) {
		const size_t pa = declPos(src, a.name, from);
		const size_t pb = declPos(src, b.name, from);
		if (pa != pb) {
			return pa < pb;
		}
		return a.name < b.name;
	});
}

auto scriptInstanceIndex(lua_State* l) -> int {
	auto* np = static_cast<NodeProxy*>(lua_touserdata(l, lua_upvalueindex(1)));
	if (!np || lua_type(l, 2) != LUA_TSTRING) {
		lua_pushnil(l);
		return 1;
	}

	const toast::NodeInfo* info = np->exists() ? np->box()->info() : nullptr;

	if (isCallableProxyKey(lua_tostring(l, 2), info)) {
		lua_pushvalue(l, 2);             // method name string
		lua_pushlightuserdata(l, np);    // NodeProxy*
		lua_pushcclosure(l, selfMethodDispatch, 2);
		return 1;
	}

	// Not a method so delegate to nodeProxyIndex which handles field reads
	luabridge::LuaRef key_ref = luabridge::LuaRef::fromStack(l, 2);
	auto result = nodeProxyIndex(*np, key_ref, l);
	result.push(l);
	return 1;
}

auto scriptInstanceNewindex(lua_State* l) -> int {
	auto* np = static_cast<NodeProxy*>(lua_touserdata(l, lua_upvalueindex(1)));

	if (np && lua_type(l, 2) == LUA_TSTRING) {
		const char* key = lua_tostring(l, 2);
		if (np->hasField(key)) {
			// Route the write through the proxy
			luabridge::LuaRef key_ref = luabridge::LuaRef::fromStack(l, 2);
			luabridge::LuaRef val_ref = luabridge::LuaRef::fromStack(l, 3);
			nodeProxyNewindex(*np, key_ref, val_ref, l);
			return 0;
		}
	}

	// Not a reflected field
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_rawset(l, 1);
	return 0;
}

}

ScriptInstance::ScriptInstance(lua_State* l, const assets::Handle<assets::Script>& script, NodeProxy proxy)
    : m_state(l),
      m_proxy(std::move(proxy)),
      m_name(script.path()) {
	if (!script.hasValue()) {
		TOAST_WARN("Lua", "ScriptInstance: script asset '{}' is not loaded", script.path());
		return;
	}

	ZoneScopedN("Lua load");    // NOLINT
	ZoneNameF("Lua load %s", m_name.c_str());

	const std::vector<uint8_t>& bytes = script->get();
	const std::string_view src(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	const std::string chunk_name = std::format("={}", script.path());

	// Load the source as a Lua chunk
	int load_status = luaL_loadbufferx(l, src.data(), src.size(), chunk_name.c_str(), nullptr);
	if (load_status != LUA_OK) {
		TOAST_ERROR("Lua", "ScriptInstance: failed to load '{}': {}", script.path(), lua_tostring(l, -1));
		lua_pop(l, 1);
		return;
	}

	// Run the chunk
	// expect exactly a lua table as a return value
	int pcall_status = pcallTraceback(l, 0, 1);
	if (pcall_status != LUA_OK) {
		TOAST_ERROR("Lua", "ScriptInstance: error running '{}': {}", script.path(), lua_tostring(l, -1));
		lua_pop(l, 1);
		return;
	}

	if (!lua_istable(l, -1)) {
		TOAST_ERROR("Lua", "ScriptInstance: '{}' did not return a table", script.path());
		lua_pop(l, 1);
		return;
	}

	// save the returned table in the Lua registry
	m_self = std::make_unique<luabridge::LuaRef>(luabridge::LuaRef::fromStack(l, -1));
	lua_pop(l, 1);
	extractSchema(src);
	snapshotTickMask();
	installMetatable();
}

void ScriptInstance::snapshotTickMask() noexcept {
	using F = toast::TickFunctionList;
	constexpr std::array all_phases = {
	  F::load,
	  F::save,
	  F::pre_init,
	  F::init,
	  F::destroy,
	  F::begin,
	  F::end,
	  F::on_enable,
	  F::on_disable,
	  F::early_tick,
	  F::tick,
	  F::post_physics,
	  F::late_tick,
	};
	for (F phase : all_phases) {
		const char* name = phaseToLuaName(phase);
		if (name && hasFunction(name)) {
			m_tick_mask |= phase;
		}
	}
}

void ScriptInstance::extractSchema(std::string_view src) noexcept {
	ZoneScopedN("Lua schema");    // NOLINT

	m_schema = {};
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* l = m_state;

	auto classify =
	    [&](const std::string& name, std::string_view path_prefix, const luabridge::LuaRef& val) -> std::optional<LuaVarDesc> {
		luabridge::LuaRef element = val;
		bool is_array = false;
		if (val.isTable()) {
			element = val[1];
			if (!element.isValid() || element.isNil()) {
				return std::nullopt;    // group, or an untypeable empty table
			}
			is_array = true;
		}
		auto desc = classifyLeaf(l, element);
		if (!desc) {
			TOAST_WARN("Lua", "{}: exported var '{}' has an unsupported type; skipping", m_name, name);
			return std::nullopt;
		}
		desc->name = name;
		desc->path = path_prefix.empty() ? name : std::format("{}/{}", path_prefix, name);
		desc->is_array = is_array;
		desc->default_value = luaRefValueToAny(l, val);    // fresh, script-declared value; captured before any edit
		return desc;
	};

	for (luabridge::Iterator it(*m_self); !it.isNil(); ++it) {
		if (!isExportableKey(it.key()) || it.value().isFunction()) {
			continue;
		}
		const std::string key = it.key().tostring();
		luabridge::LuaRef val = it.value();

		// Leaf or array at the top level
		if (auto desc = classify(key, "", val)) {
			m_schema.fields.push_back(std::move(*desc));
			continue;
		}
		if (!val.isTable()) {
			continue;    // unsupported leaf
		}

		// Stringkeyed table = inspector group
		LuaGroup group;
		group.name = key;
		for (luabridge::Iterator git(val); !git.isNil(); ++git) {
			if (!isExportableKey(git.key()) || git.value().isFunction()) {
				continue;
			}
			const std::string gkey = git.key().tostring();
			luabridge::LuaRef gval = git.value();

			if (auto desc = classify(gkey, group.name, gval)) {
				group.fields.push_back(std::move(*desc));
				continue;
			}
			if (!gval.isTable()) {
				continue;
			}

			// Nested table inside a group = subgroup
			// Anything deeper is just not supported
			LuaSubgroup sub;
			sub.name = gkey;
			const std::string sub_prefix = std::format("{}/{}", group.name, sub.name);
			for (luabridge::Iterator sit(gval); !sit.isNil(); ++sit) {
				if (!isExportableKey(sit.key()) || sit.value().isFunction()) {
					continue;
				}
				const std::string skey = sit.key().tostring();
				if (auto desc = classify(skey, sub_prefix, sit.value())) {
					sub.fields.push_back(std::move(*desc));
				} else if (sit.value().isTable()) {
					TOAST_WARN("Lua", "{}: table '{}/{}' nests deeper than group/subgroup; skipping", m_name, sub_prefix, skey);
				}
			}
			group.subgroups.push_back(std::move(sub));
		}
		if (group.fields.empty() && group.subgroups.empty()) {
			TOAST_WARN("Lua", "{}: exported table '{}' is empty and cannot be typed; skipping", m_name, key);
			continue;
		}
		m_schema.groups.push_back(std::move(group));
	}

	// recover the order vars are written in the source
	sortByDeclaration(m_schema.fields, src, 0);
	sortByDeclaration(m_schema.groups, src, 0);
	for (LuaGroup& group : m_schema.groups) {
		const size_t group_pos = declPos(src, group.name, 0);
		const size_t from = group_pos == std::string_view::npos ? 0 : group_pos;
		sortByDeclaration(group.fields, src, from);
		sortByDeclaration(group.subgroups, src, from);
		for (LuaSubgroup& sub : group.subgroups) {
			const size_t sub_pos = declPos(src, sub.name, from);
			sortByDeclaration(sub.fields, src, sub_pos == std::string_view::npos ? from : sub_pos);
		}
	}
}

void ScriptInstance::installMetatable() noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* l = m_state;

	// Push self table
	m_self->push(l);
	const int self_idx = lua_gettop(l);

	// Create metatable
	lua_newtable(l);
	const int mt_idx = lua_gettop(l);

	// __index / __newindex
	// m_proxy is stable for the lifetime of this ScriptInstance
	lua_pushlightuserdata(l, &m_proxy);
	lua_pushcclosure(l, scriptInstanceIndex, 1);
	lua_setfield(l, mt_idx, "__index");

	lua_pushlightuserdata(l, &m_proxy);
	lua_pushcclosure(l, scriptInstanceNewindex, 1);
	lua_setfield(l, mt_idx, "__newindex");

	lua_setmetatable(l, self_idx);
	lua_pop(l, 1);    // pop self table
}

void ScriptInstance::call(std::string_view fn_name) noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* l = m_state;
	// instance table, so rawget finds them
	m_self->push(l);
	lua_pushlstring(l, fn_name.data(), fn_name.size());
	lua_rawget(l, -2);
	lua_remove(l, -2);    // remove self table

	if (!lua_isfunction(l, -1)) {
		lua_pop(l, 1);
		return;
	}

	ZoneScopedN("Lua call");    // NOLINT
	ZoneNameF("%s %.*s()", m_name.c_str(), static_cast<int>(fn_name.size()), fn_name.data());

	// push self, then pcall(fn, self)
	m_self->push(l);
	if (pcallTraceback(l, 1, 0) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in {}(): {}", fn_name, lua_tostring(l, -1));
		lua_pop(l, 1);
	}
}

void ScriptInstance::callWithLuaStack(std::string_view name, lua_State* l, int args_base, int n_args) noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	// Only calls functions defined in the Lua table
	m_self->push(l);
	lua_pushlstring(l, name.data(), name.size());
	lua_rawget(l, -2);
	lua_remove(l, -2);

	if (!lua_isfunction(l, -1)) {
		lua_pop(l, 1);
		return;
	}

	ZoneScopedN("Lua call");    // NOLINT
	ZoneNameF("%s %.*s()", m_name.c_str(), static_cast<int>(name.size()), name.data());

	m_self->push(l);
	for (int i = 0; i < n_args; ++i) {
		lua_pushvalue(l, args_base + i);
	}
	if (pcallTraceback(l, 1 + n_args, 0) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in fan-out for '{}': {}", name, lua_tostring(l, -1));
		lua_pop(l, 1);
	}
}

void ScriptInstance::callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* l = m_state;

	// recursion guard
	m_self->push(l);
	lua_pushlstring(l, name.data(), name.size());
	lua_rawget(l, -2);
	lua_remove(l, -2);

	if (!lua_isfunction(l, -1)) {
		lua_pop(l, 1);
		return;
	}

	ZoneScopedN("Lua call");    // NOLINT
	ZoneNameF("%s %.*s()", m_name.c_str(), static_cast<int>(name.size()), name.data());

	// Push self as receiver, then convert each std::any arg to Lua
	m_self->push(l);
	for (const auto& arg : args) {
		luabridge::LuaRef ref = anyValueToLuaRef(l, arg);
		ref.push(l);
	}

	const int n_args = 1 + static_cast<int>(args.size());
	if (pcallTraceback(l, n_args, 0) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in Lua fan-out for '{}': {}", name, lua_tostring(l, -1));
		lua_pop(l, 1);
	}
}

auto ScriptInstance::setVar(std::string_view name, const std::any& value) noexcept -> bool {
	if (!m_self || m_self->isNil()) {
		return false;
	}
	lua_State* l = m_state;

	// Only write to instances that already define this key
	m_self->push(l);
	lua_pushlstring(l, name.data(), name.size());
	lua_rawget(l, -2);
	const bool exists = !lua_isnil(l, -1);
	lua_pop(l, 1);    // pop the value

	if (!exists) {
		lua_pop(l, 1);
		return false;
	}

	// Rawset self[name] = converted value
	luabridge::LuaRef ref = anyValueToLuaRef(l, value);
	lua_pushlstring(l, name.data(), name.size());
	ref.push(l);
	lua_rawset(l, -3);
	lua_pop(l, 1);
	return true;
}

auto ScriptInstance::pushByPath(std::string_view path) const noexcept -> bool {
	if (!m_self || m_self->isNil() || path.empty()) {
		return false;
	}
	lua_State* l = m_state;

	m_self->push(l);
	size_t start = 0;
	while (true) {
		const size_t slash = path.find('/', start);
		const std::string_view segment = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
		lua_pushlstring(l, segment.data(), segment.size());
		lua_rawget(l, -2);
		lua_remove(l, -2);    // drop the parent table
		if (slash == std::string_view::npos) {
			return true;        // value (possibly nil) at top
		}
		if (lua_istable(l, -1) == 0) {
			lua_pop(l, 1);
			return false;
		}
		start = slash + 1;
	}
}

auto ScriptInstance::getVarByPath(std::string_view path) const noexcept -> std::any {
	lua_State* l = m_state;
	if (!pushByPath(path)) {
		return {};
	}
	luabridge::LuaRef ref = luabridge::LuaRef::fromStack(l, -1);
	lua_pop(l, 1);
	return luaRefValueToAny(l, ref);
}

auto ScriptInstance::setVarByPath(std::string_view path, const std::any& value) noexcept -> bool {
	if (!m_self || m_self->isNil() || path.empty()) {
		return false;
	}
	lua_State* l = m_state;

	// Split off the parent path and descend to the owning table
	const size_t last_slash = path.rfind('/');
	const std::string_view leaf = last_slash == std::string_view::npos ? path : path.substr(last_slash + 1);

	if (last_slash == std::string_view::npos) {
		m_self->push(l);
	} else {
		if (!pushByPath(path.substr(0, last_slash))) {
			return false;
		}
		if (lua_istable(l, -1) == 0) {
			lua_pop(l, 1);
			return false;
		}
	}

	// Only overwrite existing keys, same policy as setVar
	lua_pushlstring(l, leaf.data(), leaf.size());
	lua_rawget(l, -2);
	const bool exists = !lua_isnil(l, -1);
	lua_pop(l, 1);
	if (!exists) {
		lua_pop(l, 1);
		return false;
	}

	luabridge::LuaRef ref = anyValueToLuaRef(l, value);
	lua_pushlstring(l, leaf.data(), leaf.size());
	ref.push(l);
	lua_rawset(l, -3);
	lua_pop(l, 1);
	return true;
}

auto ScriptInstance::getVar(std::string_view name) const noexcept -> std::any {
	if (!m_self || m_self->isNil()) {
		return {};
	}
	lua_State* l = m_state;

	m_self->push(l);
	lua_pushlstring(l, name.data(), name.size());
	lua_rawget(l, -2);
	lua_remove(l, -2);    // value at top, self gone

	luabridge::LuaRef ref = luabridge::LuaRef::fromStack(l, -1);
	lua_pop(l, 1);

	return luaRefValueToAny(l, ref);
}

auto ScriptInstance::hasFunction(std::string_view fn_name) const noexcept -> bool {
	if (!m_self || m_self->isNil()) {
		return false;
	}
	lua_State* l = m_state;
	m_self->push(l);
	lua_pushlstring(l, fn_name.data(), fn_name.size());
	lua_rawget(l, -2);
	const bool is_fn = lua_isfunction(l, -1);
	lua_pop(l, 2);    // pop result + self table
	return is_fn;
}

ScriptRuntime::ScriptRuntime(toast::Box<toast::Node> node, const std::vector<assets::Handle<assets::Script>>& scripts) {
	if (scripts.empty()) {
		return;
	}

	m_state_index = LuaState::get().nextIndex();
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		TOAST_ERROR("Lua", "ScriptRuntime: could not acquire Lua state #{}; scripts not loaded", m_state_index);
		return;
	}
	m_lua = guard.state();
	NodeProxy proxy(std::move(node));

	m_instances.reserve(scripts.size());
	for (const auto& script : scripts) {
		if (script.hasValue()) {
			m_instances.push_back(std::make_unique<ScriptInstance>(m_lua, script, proxy));
			m_tick_mask |= m_instances.back()->tickMask();
		}
	}

	static std::atomic<uint32_t> s_schema_version {0};
	m_schema_version = ++s_schema_version;

	// Same-named vars across scripts are ambiguous for name-based get/set: first script wins
	std::unordered_map<std::string_view, std::string_view> seen;
	for (const auto& inst : m_instances) {
		for (const LuaVarDesc& field : inst->schema().fields) {
			auto [it, inserted] = seen.try_emplace(field.name, inst->name());
			if (!inserted) {
				TOAST_WARN(
				    "Lua",
				    "Variable '{}' defined by both '{}' and '{}'; '{}' takes precedence",
				    field.name,
				    it->second,
				    inst->name(),
				    it->second
				);
			}
		}
	}
}

auto ScriptRuntime::instanceSchema(size_t index) const noexcept -> const ScriptSchema* {
	if (index >= m_instances.size() || !m_instances[index] || !m_instances[index]->isValid()) {
		return nullptr;
	}
	return &m_instances[index]->schema();
}

auto ScriptRuntime::instanceScript(size_t index) const noexcept -> std::string_view {
	if (index >= m_instances.size() || !m_instances[index]) {
		return {};
	}
	return m_instances[index]->name();
}

auto ScriptRuntime::getVarByPath(size_t index, std::string_view path) const noexcept -> std::any {
	if (index >= m_instances.size() || !m_instances[index] || !m_instances[index]->isValid()) {
		return {};
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return {};
	}
	return m_instances[index]->getVarByPath(path);
}

auto ScriptRuntime::setVarByPath(size_t index, std::string_view path, const std::any& value) noexcept -> bool {
	if (index >= m_instances.size() || !m_instances[index] || !m_instances[index]->isValid()) {
		return false;
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return false;
	}
	return m_instances[index]->setVarByPath(path, value);
}

void ScriptRuntime::call(toast::TickFunctionList phase) noexcept {
	const char* name = phaseToLuaName(phase);
	if (!name || m_instances.empty() || !toast::hasFlag(m_tick_mask, phase)) {
		return;
	}

	ZoneScopedN("Lua phase");    // NOLINT
	ZoneNameF("Lua phase %s", name);

	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return;
	}
	for (auto& inst : m_instances) {
		if (inst && inst->isValid() && toast::hasFlag(inst->tickMask(), phase)) {
			inst->call(name);
		}
	}
}

void ScriptRuntime::call(std::string_view fn_name) noexcept {
	if (m_instances.empty()) {
		return;
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return;
	}
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->call(fn_name);
		}
	}
}

auto ScriptRuntime::hasFunction(std::string_view fn_name) const noexcept -> bool {
	if (m_instances.empty()) {
		return false;
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return false;
	}
	for (const auto& inst : m_instances) {
		if (inst && inst->isValid() && inst->hasFunction(fn_name)) {
			return true;
		}
	}
	return false;
}

void ScriptRuntime::callWithLuaStack(std::string_view name, lua_State* l, int args_base, int n_args) noexcept {
	if (m_instances.empty()) {
		return;
	}

	if (l != m_lua) {
		std::vector<std::any> args;
		args.reserve(static_cast<size_t>(n_args));
		for (int i = 0; i < n_args; ++i) {
			luabridge::LuaRef ref = luabridge::LuaRef::fromStack(l, args_base + i);
			args.push_back(luaRefValueToAny(l, ref));
		}
		callWithAnyArgs(name, args);
		return;
	}

	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return;
	}
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->callWithLuaStack(name, l, args_base, n_args);
		}
	}
}

void ScriptRuntime::callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept {
	if (m_instances.empty()) {
		return;
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return;
	}
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->callWithAnyArgs(name, args);
		}
	}
}

void ScriptRuntime::setVar(std::string_view name, const std::any& value) noexcept {
	if (m_instances.empty()) {
		return;
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return;
	}
	// First instance defining the name owns it, matching getVar's resolution order
	for (auto& inst : m_instances) {
		if (inst && inst->isValid() && inst->setVar(name, value)) {
			return;
		}
	}
}

auto ScriptRuntime::getVar(std::string_view name) const noexcept -> std::any {
	if (m_instances.empty()) {
		return {};
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return {};
	}
	for (const auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			std::any v = inst->getVar(name);
			if (v.has_value()) {
				return v;
			}
		}
	}
	return {};
}

}
