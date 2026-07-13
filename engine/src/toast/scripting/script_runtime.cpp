#include "script_runtime.hpp"

#include "lua_state.hpp"
#include "node_proxy.hpp"

#include <format>
#include <toast/log.hpp>
#include <toast/world/node.hpp>

namespace scripting {

namespace {

bool isCallableProxyKey(std::string_view key, const toast::NodeInfo* info) noexcept {
	// Builtin methods
	if (key == "find" || key == "search" || key == "create" || key == "exists" || key == "name" || key == "uid" ||
	    key == "addDependsOn" || key == "call" || key == "enabled") {
		return true;
	}
	return info != nullptr && info->getMethod(key) != nullptr;
}

int selfMethodDispatch(lua_State* L) {
	const char* name = lua_tostring(L, lua_upvalueindex(1));
	auto* np = static_cast<NodeProxy*>(lua_touserdata(L, lua_upvalueindex(2)));
	if (!np || !name) {
		if (name) {
			luaL_error(L, "method '%s': no NodeProxy available", name);
		}
		return 0;
	}
	const int n_args = lua_gettop(L) - 1;    // exclude self at slot 1
	const int args_base = 2;
	return nodeProxyDispatchMethod(*np, name, L, args_base, n_args);
}

const char* phaseToLuaName(toast::TickFunctionList phase) noexcept {
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

int scriptInstanceIndex(lua_State* L) {
	auto* np = static_cast<NodeProxy*>(lua_touserdata(L, lua_upvalueindex(1)));
	if (!np || lua_type(L, 2) != LUA_TSTRING) {
		lua_pushnil(L);
		return 1;
	}

	const toast::NodeInfo* info = np->exists() ? np->box()->info() : nullptr;

	if (isCallableProxyKey(lua_tostring(L, 2), info)) {
		lua_pushvalue(L, 2);             // method name string
		lua_pushlightuserdata(L, np);    // NodeProxy*
		lua_pushcclosure(L, selfMethodDispatch, 2);
		return 1;
	}

	// Not a method so delegate to nodeProxyIndex which handles field reads
	luabridge::LuaRef key_ref = luabridge::LuaRef::fromStack(L, 2);
	auto result = nodeProxyIndex(*np, key_ref, L);
	result.push(L);
	return 1;
}

int scriptInstanceNewindex(lua_State* L) {
	auto* np = static_cast<NodeProxy*>(lua_touserdata(L, lua_upvalueindex(1)));

	if (np && lua_type(L, 2) == LUA_TSTRING) {
		const char* key = lua_tostring(L, 2);
		if (np->hasField(key)) {
			// Route the write through the proxy
			luabridge::LuaRef key_ref = luabridge::LuaRef::fromStack(L, 2);
			luabridge::LuaRef val_ref = luabridge::LuaRef::fromStack(L, 3);
			nodeProxyNewindex(*np, key_ref, val_ref, L);
			return 0;
		}
	}

	// Not a reflected field
	lua_pushvalue(L, 2);
	lua_pushvalue(L, 3);
	lua_rawset(L, 1);
	return 0;
}

}

ScriptInstance::ScriptInstance(lua_State* L, const assets::AssetHandle<assets::Script>& script, const NodeProxy& proxy)
    : m_state(L),
      m_proxy(proxy) {
	if (!script.hasValue()) {
		TOAST_WARN("Lua", "ScriptInstance: script asset '{}' is not loaded", script.path());
		return;
	}

	const std::vector<uint8_t>& bytes = script->get();
	const std::string_view src(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	const std::string chunk_name = std::format("={}", script.path());

	// Load the source as a Lua chunk
	int load_status = luaL_loadbufferx(L, src.data(), src.size(), chunk_name.c_str(), nullptr);
	if (load_status != LUA_OK) {
		TOAST_ERROR("Lua", "ScriptInstance: failed to load '{}': {}", script.path(), lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}

	// Run the chunk
	// expect exactly a lua table as a return value
	int pcall_status = lua_pcall(L, 0, 1, 0);
	if (pcall_status != LUA_OK) {
		TOAST_ERROR("Lua", "ScriptInstance: error running '{}': {}", script.path(), lua_tostring(L, -1));
		lua_pop(L, 1);
		return;
	}

	if (!lua_istable(L, -1)) {
		TOAST_ERROR("Lua", "ScriptInstance: '{}' did not return a table", script.path());
		lua_pop(L, 1);
		return;
	}

	// save the returned table in the Lua registry
	m_self = std::make_unique<luabridge::LuaRef>(luabridge::LuaRef::fromStack(L, -1));
	lua_pop(L, 1);
	snapshotSchema();
	installMetatable();
}

void ScriptInstance::snapshotSchema() noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	for (luabridge::Iterator it(*m_self); !it.isNil(); ++it) {
		luabridge::LuaRef key = it.key();
		luabridge::LuaRef val = it.value();
		if (!key.isString() || val.isFunction()) {
			continue;
		}
		m_schema.push_back({key.tostring(), val});
	}
}

void ScriptInstance::installMetatable() noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* L = m_state;

	// Push self table
	m_self->push(L);
	const int self_idx = lua_gettop(L);

	// Create metatable
	lua_newtable(L);
	const int mt_idx = lua_gettop(L);

	// __index / __newindex
	// m_proxy is stable for the lifetime of this ScriptInstance
	lua_pushlightuserdata(L, &m_proxy);
	lua_pushcclosure(L, scriptInstanceIndex, 1);
	lua_setfield(L, mt_idx, "__index");

	lua_pushlightuserdata(L, &m_proxy);
	lua_pushcclosure(L, scriptInstanceNewindex, 1);
	lua_setfield(L, mt_idx, "__newindex");

	lua_setmetatable(L, self_idx);
	lua_pop(L, 1);    // pop self table
}

void ScriptInstance::call(std::string_view fn_name) noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* L = m_state;
	// instance table, so rawget finds them
	m_self->push(L);
	lua_pushlstring(L, fn_name.data(), fn_name.size());
	lua_rawget(L, -2);
	lua_remove(L, -2);    // remove self table

	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	// push self, then pcall(fn, self)
	m_self->push(L);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in {}(): {}", fn_name, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

void ScriptInstance::callWithLuaStack(std::string_view name, lua_State* L, int args_base, int n_args) noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	// Only calls functions defined in the Lua table
	m_self->push(L);
	lua_pushlstring(L, name.data(), name.size());
	lua_rawget(L, -2);
	lua_remove(L, -2);

	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	m_self->push(L);
	for (int i = 0; i < n_args; ++i) {
		lua_pushvalue(L, args_base + i);
	}
	if (lua_pcall(L, 1 + n_args, 0, 0) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in fan-out for '{}': {}", name, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

void ScriptInstance::callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* L = m_state;

	// recursion guard
	m_self->push(L);
	lua_pushlstring(L, name.data(), name.size());
	lua_rawget(L, -2);
	lua_remove(L, -2);nil) at top

	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	// Push self as receiver, then convert each std::any arg to Lua
	m_self->push(L);
	for (const auto& arg : args) {
		luabridge::LuaRef ref = anyValueToLuaRef(L, arg);
		ref.push(L);
	}

	const int n_args = 1 + static_cast<int>(args.size());
	if (lua_pcall(L, n_args, 0, 0) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in Lua fan-out for '{}': {}", name, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

void ScriptInstance::setVar(std::string_view name, const std::any& value) noexcept {
	if (!m_self || m_self->isNil()) {
		return;
	}
	lua_State* L = m_state;

	// Only write to instances that already define this key
	m_self->push(L);
	lua_pushlstring(L, name.data(), name.size());
	lua_rawget(L, -2);
	const bool exists = !lua_isnil(L, -1);
	lua_pop(L, 1);    // pop the value

	if (!exists) {
		lua_pop(L, 1);
		return;
	}

	// Rawset self[name] = converted value
	luabridge::LuaRef ref = anyValueToLuaRef(L, value);
	lua_pushlstring(L, name.data(), name.size());
	ref.push(L);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}

auto ScriptInstance::getVar(std::string_view name) const noexcept -> std::any {
	if (!m_self || m_self->isNil()) {
		return {};
	}
	lua_State* L = m_state;

	m_self->push(L);
	lua_pushlstring(L, name.data(), name.size());
	lua_rawget(L, -2);
	lua_remove(L, -2);    // value at top, self gone

	luabridge::LuaRef ref = luabridge::LuaRef::fromStack(L, -1);
	lua_pop(L, 1);

	return luaRefValueToAny(L, ref);
}

bool ScriptInstance::hasFunction(std::string_view fn_name) const noexcept {
	if (!m_self || m_self->isNil()) {
		return false;
	}
	lua_State* L = m_state;
	m_self->push(L);
	lua_pushlstring(L, fn_name.data(), fn_name.size());
	lua_rawget(L, -2);
	const bool is_fn = lua_isfunction(L, -1);
	lua_pop(L, 2);    // pop result + self table
	return is_fn;
}

ScriptRuntime::ScriptRuntime(toast::Box<toast::Node> node, const std::vector<assets::AssetHandle<assets::Script>>& scripts) {
	if (scripts.empty()) {
		return;
	}
	// Confirm LuaState exists
	lua_State* L = LuaState::get().state();
	NodeProxy proxy(std::move(node));

	m_instances.reserve(scripts.size());
	for (const auto& script : scripts) {
		if (script.hasValue()) {
			m_instances.push_back(std::make_unique<ScriptInstance>(L, script, proxy));
		}
	}
}

void ScriptRuntime::call(toast::TickFunctionList phase) noexcept {
	const char* name = phaseToLuaName(phase);
	if (!name) {
		return;
	}
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->call(name);
		}
	}
}

void ScriptRuntime::call(std::string_view fn_name) noexcept {
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->call(fn_name);
		}
	}
}

void ScriptRuntime::callWithLuaStack(std::string_view name, lua_State* L, int args_base, int n_args) noexcept {
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->callWithLuaStack(name, L, args_base, n_args);
		}
	}
}

void ScriptRuntime::callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept {
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->callWithAnyArgs(name, args);
		}
	}
}

void ScriptRuntime::setVar(std::string_view name, const std::any& value) noexcept {
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->setVar(name, value);
		}
	}
}

auto ScriptRuntime::getVar(std::string_view name) const noexcept -> std::any {
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

bool ScriptRuntime::hasTick(toast::TickFunctionList mask) const noexcept {
	using F = toast::TickFunctionList;
	constexpr F kTickPhases[] = {F::early_tick, F::tick, F::post_physics, F::late_tick};
	for (auto& inst : m_instances) {
		if (!inst || !inst->isValid()) {
			continue;
		}
		for (F phase : kTickPhases) {
			if (toast::hasFlag(mask, phase)) {
				const char* name = phaseToLuaName(phase);
				if (name && inst->hasFunction(name)) {
					return true;
				}
			}
		}
	}
	return false;
}

}
