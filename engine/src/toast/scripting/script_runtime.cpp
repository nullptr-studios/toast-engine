#include "script_runtime.hpp"

#include "asset_proxy.hpp"
#include "lua_event.hpp"
#include "lua_signal.hpp"
#include "lua_state.hpp"
#include "lua_types.hpp"
#include "lua_util.hpp"
#include "node_proxy.hpp"
#include "script_context.hpp"
#include "script_dispatch.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <format>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <toast/log.hpp>
#include <toast/world/node.hpp>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scripting {

namespace {

auto isCallableProxyKey(std::string_view key, const toast::NodeInfo* info) noexcept -> bool {
	// Builtin methods
	if (key == "find" || key == "search" || key == "parent" || key == "root" || key == "searchType" || key == "getChildren" ||
	    key == "create" || key == "exists" || key == "name" || key == "uid" || key == "addDependsOn" || key == "call" ||
	    key == "enabled") {
		return true;
	}
	return info != nullptr && info->getMethod(key) != nullptr;
}

constexpr const char* k_binding_metatable = "toast.ScriptBinding";
constexpr const char* k_unloaded_instance_message =
    "this script instance was unloaded (the script was reloaded or its node destroyed); stale references to self cannot be used";

/// Pushes a userdata that keeps the binding of an instance alive for as long as Lua references it
void pushBinding(lua_State* l, const std::shared_ptr<ScriptBinding>& binding) {
	void* memory = lua_newuserdatauv(l, sizeof(std::shared_ptr<ScriptBinding>), 0);
	new (memory) std::shared_ptr<ScriptBinding>(binding);
	if (luaL_newmetatable(l, k_binding_metatable) != 0) {
		lua_pushcfunction(l, [](lua_State* state) -> int {
			std::destroy_at(static_cast<std::shared_ptr<ScriptBinding>*>(lua_touserdata(state, 1)));
			return 0;
		});
		lua_setfield(l, -2, "__gc");
	}
	lua_setmetatable(l, -2);
}

/// The binding stored in the userdata at `index`, or null when that is not a binding
auto bindingAt(lua_State* l, int index) -> ScriptBinding* {
	auto* handle = static_cast<std::shared_ptr<ScriptBinding>*>(lua_touserdata(l, index));
	return handle != nullptr ? handle->get() : nullptr;
}

/// Like bindingAt(), but null once the instance that created the binding is gone
auto liveBinding(lua_State* l, int index) -> ScriptBinding* {
	ScriptBinding* binding = bindingAt(l, index);
	return binding != nullptr && binding->alive.load(std::memory_order_acquire) ? binding : nullptr;
}

auto selfMethodDispatch(lua_State* l) -> int {
	const char* name = lua_tostring(l, lua_upvalueindex(1));
	ScriptBinding* binding = liveBinding(l, lua_upvalueindex(2));
	if (binding == nullptr || name == nullptr) {
		return luaL_error(l, "method '%s': %s", name != nullptr ? name : "?", k_unloaded_instance_message);
	}
	const int n_args = lua_gettop(l) - 1;    // exclude self at slot 1
	const int args_base = 2;
	return nodeProxyDispatchMethod(binding->proxy, name, l, args_base, n_args);
}

auto phaseToLuaName(toast::TickFunctionList phase) noexcept -> const char* {
	using F = toast::TickFunctionList;
	switch (phase) {
		case F::load: return "load";
		case F::save: return "save";
		case F::editor_tick: return "editorTick";
		case F::init: return "init";
		case F::destroy: return "destroy";
		case F::begin: return "begin";
		case F::end: return "_end";    // "end" is a reserved Lua keyword
		case F::on_enable: return "onEnable";
		case F::on_disable: return "onDisable";
		case F::early_tick: return "earlyTick";
		case F::tick: return "tick";
		case F::physics_tick: return "physicsTick";
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

auto trim(std::string_view text) -> std::string_view {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
		text.remove_prefix(1);
	}
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
		text.remove_suffix(1);
	}
	return text;
}

auto annotationString(std::string_view value) -> std::string {
	value = trim(value);
	if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
		value.remove_prefix(1);
		value.remove_suffix(1);
	}
	return std::string(value);
}

auto parseNumber(std::string_view& text, double& value) -> bool {
	text = trim(text);
	if (text.empty()) {
		return false;
	}
	const char* begin = text.data();
	const char* end = begin + text.size();
	auto [next, error] = std::from_chars(begin, end, value);
	if (error != std::errc {} || next == begin) {
		return false;
	}
	text.remove_prefix(static_cast<size_t>(next - begin));
	return true;
}

void applyFieldAnnotations(LuaVarDesc& desc, std::string_view src, size_t declaration_pos, std::string_view script_name) {
	if (declaration_pos == std::string_view::npos) {
		return;
	}

	const size_t declaration_line = src.rfind('\n', declaration_pos);
	size_t line_start = declaration_line == std::string_view::npos ? 0 : declaration_line + 1;
	std::vector<std::string_view> annotations;
	while (line_start > 0) {
		const size_t line_end = line_start - 1;
		const size_t previous_newline = line_end == 0 ? std::string_view::npos : src.rfind('\n', line_end - 1);
		const size_t previous_start = previous_newline == std::string_view::npos ? 0 : previous_newline + 1;
		const std::string_view line = trim(src.substr(previous_start, line_end - previous_start));

		if (line.empty()) {
			line_start = previous_start;
			continue;
		}
		if (!line.starts_with("---@")) {
			break;
		}
		annotations.push_back(line.substr(4));
		line_start = previous_start;
	}
	std::ranges::reverse(annotations);

	for (std::string_view annotation : annotations) {
		annotation = trim(annotation);
		const size_t separator = annotation.find_first_of(" \t");
		const std::string_view tag = annotation.substr(0, separator);
		const std::string_view arguments =
		    separator == std::string_view::npos ? std::string_view {} : trim(annotation.substr(separator));

		if (tag == "name") {
			desc.display_name = annotationString(arguments);
			if (desc.display_name.empty()) {
				TOAST_WARN("Lua", "{}: @name on '{}' needs a display name; ignoring", script_name, desc.path);
			}
		} else if (tag == "readonly") {
			desc.read_only = true;
		} else if (tag == "hidden") {
			desc.hidden = true;
		} else if (tag == "unit") {
			desc.unit = annotationString(arguments);
			if (desc.unit.empty()) {
				TOAST_WARN("Lua", "{}: @unit on '{}' needs a unit; ignoring", script_name, desc.path);
			}
		} else if (tag == "range") {
			std::string_view rest = arguments;
			double min = 0.0;
			double max = 0.0;
			if (!parseNumber(rest, min) || !parseNumber(rest, max) || !trim(rest).empty() || min > max) {
				TOAST_WARN("Lua", "{}: invalid @range on '{}'; expected two numbers with min <= max", script_name, desc.path);
				continue;
			}
			desc.min = min;
			desc.max = max;
		}
	}
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
	ScriptBinding* binding = liveBinding(l, lua_upvalueindex(1));
	if (binding == nullptr) {
		return luaL_error(l, k_unloaded_instance_message);
	}
	NodeProxy* np = &binding->proxy;
	if (lua_type(l, 2) != LUA_TSTRING) {
		lua_pushnil(l);
		return 1;
	}

	const toast::NodeInfo* info = np->exists() ? np->box()->info() : nullptr;

	if (isCallableProxyKey(lua_tostring(l, 2), info)) {
		lua_pushvalue(l, 2);                      // method name string
		lua_pushvalue(l, lua_upvalueindex(1));    // binding handle
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
	ScriptBinding* binding = liveBinding(l, lua_upvalueindex(1));
	if (binding == nullptr) {
		return luaL_error(l, k_unloaded_instance_message);
	}
	NodeProxy* np = &binding->proxy;

	if (lua_type(l, 2) == LUA_TSTRING) {
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

ScriptInstance::ScriptInstance(
    size_t vm_index, lua_State* l, const assets::Handle<assets::Script>& script, NodeProxy proxy,
    std::shared_ptr<RuntimeToken> token
)
    : m_state(l),
      m_vm_index(vm_index),
      m_binding(std::make_shared<ScriptBinding>(std::move(proxy))),
      m_token(std::move(token)),
      m_name(script.path()) {
	if (!script.hasValue()) {
		TOAST_WARN("Lua", "ScriptInstance: script asset '{}' is not loaded", script.path());
		return;
	}

	TOAST_LUA_ASSERT_OWNED(l);
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
	int pcall_status = LUA_OK;
	{
		const ScriptNodeContextScope load_context(m_binding->proxy.box(), m_token);
		pcall_status = pcallTraceback(l, 0, 1);
	}
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
	m_valid = true;
	extractSchema(src);
	snapshotTickMask();
	installMetatable();
}

auto nodeOfSelfTable(const luabridge::LuaRef& table) -> NodeProxy {
	if (!table.isTable()) {
		return {};
	}
	lua_State* l = table.state();
	const int top = lua_gettop(l);
	NodeProxy result;
	table.push(l);
	if (lua_getmetatable(l, -1) != 0) {
		lua_getfield(l, -1, "__index");
		// The metatable of a self table carries the binding as the first upvalue of its __index closure
		if (lua_iscfunction(l, -1) != 0 && lua_tocfunction(l, -1) == scriptInstanceIndex && lua_getupvalue(l, -1, 1) != nullptr) {
			if (const ScriptBinding* binding = liveBinding(l, -1)) {
				result = binding->proxy;
			}
		}
	}
	lua_settop(l, top);
	return result;
}

ScriptInstance::~ScriptInstance() {
	if (m_binding) {
		// Closures that Lua keeps alive past this point fail with a clear error instead of reading freed memory
		m_binding->alive.store(false, std::memory_order_release);
	}
	if (m_self) {
		TOAST_LUA_ASSERT_OWNED(m_state);
	}
}

void ScriptInstance::snapshotTickMask() noexcept {
	using F = toast::TickFunctionList;
	constexpr std::array all_phases = {
	  F::load,
	  F::save,
	  F::editor_tick,
	  F::init,
	  F::destroy,
	  F::begin,
	  F::end,
	  F::on_enable,
	  F::on_disable,
	  F::early_tick,
	  F::tick,
	  F::physics_tick,
	  F::post_physics,
	  F::late_tick,
	};
	for (F phase : all_phases) {
		const char* name = phaseToLuaName(phase);
		if (name && knowsFunction(name)) {
			m_tick_mask |= phase;
		}
	}
}

void ScriptInstance::extractSchema(std::string_view src) noexcept {
	ZoneScopedN("Lua schema");    // NOLINT

	m_schema = {};
	if (!m_valid) {
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
		if (it.key().isString() && it.value().isFunction()) {
			m_function_names.insert(it.key().tostring());
		}
		if (!isExportableKey(it.key())) {
			continue;
		}
		const std::string key = it.key().tostring();
		luabridge::LuaRef val = it.value();
		if (val.isFunction()) {
			LuaFunctionDesc function;
			function.name = key;
			val.push(l);
			lua_Debug debug {};
			if (lua_getinfo(l, ">u", &debug) != 0) {
				const int parameter_count = std::max(0, static_cast<int>(debug.nparams) - 1);
				function.is_vararg = debug.isvararg != 0;
				for (int i = 0; i < parameter_count; ++i) {
					function.parameters.push_back(std::format("arg{}", i + 1));
				}
			}
			m_schema.functions.push_back(std::move(function));
			continue;
		}
		if (val.isInstance<LuaSignal>()) {
			auto signal = val.unsafe_cast<LuaSignal>();
			signal.owner(m_binding->proxy.box());
			m_lua_signals.emplace(key, std::move(signal));
			continue;
		}

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
	sortByDeclaration(m_schema.functions, src, 0);
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

	for (LuaVarDesc& field : m_schema.fields) {
		applyFieldAnnotations(field, src, declPos(src, field.name, 0), m_name);
	}
	for (LuaGroup& group : m_schema.groups) {
		const size_t group_pos = declPos(src, group.name, 0);
		const size_t group_from = group_pos == std::string_view::npos ? 0 : group_pos;
		for (LuaVarDesc& field : group.fields) {
			applyFieldAnnotations(field, src, declPos(src, field.name, group_from), m_name);
		}
		for (LuaSubgroup& subgroup : group.subgroups) {
			const size_t subgroup_pos = declPos(src, subgroup.name, group_from);
			const size_t subgroup_from = subgroup_pos == std::string_view::npos ? group_from : subgroup_pos;
			for (LuaVarDesc& field : subgroup.fields) {
				applyFieldAnnotations(field, src, declPos(src, field.name, subgroup_from), m_name);
			}
		}
	}
}

void ScriptInstance::installMetatable() noexcept {
	if (!m_self) {
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
	// The closures share the binding, so they stay valid for as long as Lua keeps them, even past this instance
	pushBinding(l, m_binding);
	lua_pushcclosure(l, scriptInstanceIndex, 1);
	lua_setfield(l, mt_idx, "__index");

	pushBinding(l, m_binding);
	lua_pushcclosure(l, scriptInstanceNewindex, 1);
	lua_setfield(l, mt_idx, "__newindex");

	lua_setmetatable(l, self_idx);
	lua_pop(l, 1);    // pop self table
}

void ScriptInstance::call(std::string_view fn_name) noexcept {
	if (!m_valid) {
		return;
	}
	TOAST_LUA_ASSERT_OWNED(m_state);
	const ScriptDepthGuard depth;
	if (!depth.allowed()) {
		TOAST_ERROR("Lua", "{}: {}() skipped, script calls are nested too deep (call loop?)", m_name, fn_name);
		return;
	}
	const ScriptNodeContextScope script_node_ctx(m_binding->proxy.box(), m_token);
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
	if (!m_valid) {
		return;
	}
	TOAST_LUA_ASSERT_OWNED(l);
	const ScriptDepthGuard depth;
	if (!depth.allowed()) {
		TOAST_ERROR("Lua", "{}: {}() skipped, script calls are nested too deep (call loop?)", m_name, name);
		return;
	}
	const ScriptNodeContextScope script_node_ctx(m_binding->proxy.box(), m_token);
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

auto ScriptInstance::callEventMethod(std::string_view name, lua_State* l, int event_index) noexcept -> bool {
	if (!m_valid) {
		return false;
	}
	TOAST_LUA_ASSERT_OWNED(l);
	const ScriptDepthGuard depth;
	if (!depth.allowed()) {
		TOAST_ERROR("Lua", "{}: event method '{}' skipped, script calls are nested too deep (call loop?)", m_name, name);
		return false;
	}
	const ScriptNodeContextScope script_node_ctx(m_binding->proxy.box(), m_token);
	m_self->push(l);
	lua_pushlstring(l, name.data(), name.size());
	lua_rawget(l, -2);
	lua_remove(l, -2);
	if (!lua_isfunction(l, -1)) {
		lua_pop(l, 1);
		return false;
	}
	m_self->push(l);
	lua_pushvalue(l, event_index);
	if (pcallTraceback(l, 2, 1) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in event method '{}': {}", name, lua_tostring(l, -1));
		lua_pop(l, 1);
		return false;
	}
	const bool consumed = lua_isboolean(l, -1) && lua_toboolean(l, -1) != 0;
	lua_pop(l, 1);
	return consumed;
}

void ScriptInstance::callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept {
	if (!m_valid) {
		return;
	}
	TOAST_LUA_ASSERT_OWNED(m_state);
	const ScriptDepthGuard depth;
	if (!depth.allowed()) {
		TOAST_ERROR("Lua", "{}: {}() skipped, script calls are nested too deep (call loop?)", m_name, name);
		return;
	}
	const ScriptNodeContextScope script_node_ctx(m_binding->proxy.box(), m_token);
	lua_State* l = m_state;

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
	if (!m_valid) {
		return false;
	}
	TOAST_LUA_ASSERT_OWNED(m_state);
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
	if (!m_valid || path.empty()) {
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
	TOAST_LUA_ASSERT_OWNED(m_state);
	lua_State* l = m_state;
	if (!pushByPath(path)) {
		return {};
	}
	luabridge::LuaRef ref = luabridge::LuaRef::fromStack(l, -1);
	lua_pop(l, 1);
	return luaRefValueToAny(l, ref);
}

auto ScriptInstance::setVarByPath(std::string_view path, const std::any& value) noexcept -> bool {
	if (!m_valid || path.empty()) {
		return false;
	}
	TOAST_LUA_ASSERT_OWNED(m_state);
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
	if (!m_valid) {
		return {};
	}
	TOAST_LUA_ASSERT_OWNED(m_state);
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
	if (!m_valid) {
		return false;
	}
	TOAST_LUA_ASSERT_OWNED(m_state);
	lua_State* l = m_state;
	m_self->push(l);
	lua_pushlstring(l, fn_name.data(), fn_name.size());
	lua_rawget(l, -2);
	const bool is_fn = lua_isfunction(l, -1);
	lua_pop(l, 2);    // pop result + self table
	return is_fn;
}

namespace {

/// Counts the calls that are running inside a runtime, see ScriptRuntime::executing()
class ActiveCallScope {
public:
	explicit ActiveCallScope(std::atomic<int>& counter) noexcept : m_counter(counter) { ++m_counter; }

	~ActiveCallScope() { --m_counter; }

	ActiveCallScope(const ActiveCallScope&) = delete;
	auto operator=(const ActiveCallScope&) -> ActiveCallScope& = delete;
	ActiveCallScope(ActiveCallScope&&) = delete;
	auto operator=(ActiveCallScope&&) -> ActiveCallScope& = delete;

private:
	std::atomic<int>& m_counter;
};

std::atomic<uint32_t> g_next_runtime_id {0};           // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
auto& g_retired_runtimes_mutex = *new std::mutex();    // NOLINT
auto& g_retired_runtimes = *new std::vector<std::unique_ptr<ScriptRuntime>>();    // NOLINT

}

ScriptRuntime::ScriptRuntime(
    toast::Box<toast::Node> node, const std::vector<assets::Handle<assets::Script>>& scripts, uint64_t group
)
    : m_node(std::move(node)) {
	ZoneScoped;
	if (scripts.empty()) {
		return;
	}

	m_schema_version = ++g_next_runtime_id;
	m_token = std::make_shared<RuntimeToken>();
	m_token->id = m_schema_version;

	LuaState& pool = LuaState::get();
	m_state_index = group != 0 ? pool.indexForGroup(group) : pool.nextIndex();
	if (const std::optional<size_t> owned = LuaState::lastOwnedIndex();
	    owned.has_value() && !LuaState::ownedByCurrentThread(m_state_index)) {
		// A thread that owns an interpreter cannot wait for another one, so the runtime is built on the one it has
		m_state_index = *owned;
	}
	m_token->vm_index = m_state_index;

	LuaState::Lock guard = pool.lock(m_state_index);
	if (!guard) {
		TOAST_ERROR("Lua", "ScriptRuntime: could not acquire Lua state #{}; scripts not loaded", m_state_index);
		m_token->alive.store(false, std::memory_order_release);
		return;
	}
	m_lua = guard.state();
	NodeProxy proxy(m_node);

	m_instances.reserve(scripts.size());
	for (const auto& script : scripts) {
		if (script.hasValue()) {
			m_instances.push_back(std::make_unique<ScriptInstance>(m_state_index, m_lua, script, proxy, m_token));
			m_tick_mask |= m_instances.back()->tickMask();
		}
	}

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

ScriptRuntime::~ScriptRuntime() {
	if (m_token) {
		// Everything C++ kept that calls into these scripts stops working from here on
		m_token->alive.store(false, std::memory_order_release);
	}
	clearLuaEventSubscriptions(this);
	if (m_instances.empty() || m_lua == nullptr) {
		return;
	}
	// The instances hold registry references and userdata, which only the owner of the interpreter may release
	LuaState::retireOrLeak(m_state_index, [instances = std::move(m_instances)]() mutable { instances.clear(); });
}

template<typename F>
void ScriptRuntime::deferToDispatch(F&& work) const {
	ScriptDispatch::enqueue(m_state_index, [node = m_node, id = m_schema_version, work = std::forward<F>(work)]() mutable {
		ScriptRuntime* runtime = node.exists() ? node->scriptRuntime() : nullptr;
		if (runtime != nullptr && runtime->schemaVersion() == id) {
			work(*runtime);
		}
	});
}

void ScriptRuntime::retire(std::unique_ptr<ScriptRuntime> runtime) noexcept {
	if (!runtime) {
		return;
	}
	if (!runtime->executing()) {
		runtime.reset();
		return;
	}
	// The call that is still running finishes, but nothing new reaches these scripts any more
	if (runtime->m_token) {
		runtime->m_token->alive.store(false, std::memory_order_release);
	}
	std::scoped_lock lock(g_retired_runtimes_mutex);
	g_retired_runtimes.push_back(std::move(runtime));
}

void ScriptRuntime::drainRetired() noexcept {
	std::vector<std::unique_ptr<ScriptRuntime>> finished;
	{
		std::scoped_lock lock(g_retired_runtimes_mutex);
		if (g_retired_runtimes.empty()) {
			return;
		}
		auto split = std::ranges::partition(g_retired_runtimes, [](const auto& runtime) { return runtime->executing(); });
		finished.assign(std::make_move_iterator(split.begin()), std::make_move_iterator(split.end()));
		g_retired_runtimes.erase(split.begin(), split.end());
	}
	finished.clear();
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
		TOAST_WARN("Lua", "Lua state #{} is busy; could not read '{}'", m_state_index, path);
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
		TOAST_WARN("Lua", "Lua state #{} is busy; could not write '{}'", m_state_index, path);
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
		deferToDispatch([phase](ScriptRuntime& runtime) { runtime.call(phase); });
		return;
	}
	const ActiveCallScope active(m_active_calls);
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
		deferToDispatch([name = std::string(fn_name)](ScriptRuntime& runtime) { runtime.call(std::string_view(name)); });
		return;
	}
	const ActiveCallScope active(m_active_calls);
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
	// What the scripts defined at load needs no interpreter, which keeps lookups across busy interpreters cheap
	for (const auto& inst : m_instances) {
		if (inst && inst->isValid() && inst->knowsFunction(fn_name)) {
			return true;
		}
	}
	// A script can also add functions while it runs, and only the owner of the interpreter can see those
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

auto ScriptRuntime::functions() const noexcept -> std::vector<LuaFunctionDesc> {
	std::vector<LuaFunctionDesc> result;
	for (const auto& instance : m_instances) {
		if (!instance || !instance->isValid()) {
			continue;
		}
		for (const auto& function : instance->schema().functions) {
			auto existing = std::ranges::find(result, function.name, &LuaFunctionDesc::name);
			if (existing == result.end()) {
				result.push_back(function);
			} else {
				existing->is_vararg = existing->is_vararg || function.is_vararg;
			}
		}
	}
	return result;
}

void ScriptRuntime::callWithLuaStack(std::string_view name, lua_State* l, int args_base, int n_args) noexcept {
	if (m_instances.empty()) {
		return;
	}

	if (l != m_lua) {
		// Another interpreter or a coroutine: only plain values can cross, and they are read while the caller owns l
		std::vector<std::any> args;
		args.reserve(static_cast<size_t>(n_args));
		for (int i = 0; i < n_args; ++i) {
			luabridge::LuaRef ref = luabridge::LuaRef::fromStack(l, args_base + i);
			args.push_back(luaRefValueToAny(l, ref));
		}
		callWithAnyArgs(name, args);
		return;
	}

	// The caller owns the interpreter of this runtime, so this lock is a recursive one
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return;
	}
	const ActiveCallScope active(m_active_calls);
	for (auto& inst : m_instances) {
		if (inst && inst->isValid()) {
			inst->callWithLuaStack(name, l, args_base, n_args);
		}
	}
}

auto ScriptRuntime::callEventMethod(std::string_view name, lua_State* l, int event_index) noexcept -> bool {
	if (m_instances.empty() || l != m_lua) {
		return false;
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		return false;
	}
	const ActiveCallScope active(m_active_calls);
	for (auto& instance : m_instances) {
		if (instance && instance->isValid() && instance->callEventMethod(name, l, event_index)) {
			return true;
		}
	}
	return false;
}

void ScriptRuntime::callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept {
	if (m_instances.empty()) {
		return;
	}
	LuaState::Lock guard = LuaState::get().lock(m_state_index);
	if (!guard) {
		// This thread owns another interpreter and cannot wait for this one, so the call is delivered later
		deferToDispatch([name = std::string(name), args = std::vector<std::any>(args.begin(), args.end())](ScriptRuntime& runtime) {
			runtime.callWithAnyArgs(name, args);
		});
		return;
	}
	const ActiveCallScope active(m_active_calls);
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
		deferToDispatch([name = std::string(name), value](ScriptRuntime& runtime) { runtime.setVar(name, value); });
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
		TOAST_WARN("Lua", "Lua state #{} is busy; could not read '{}'", m_state_index, name);
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

// The signals are plain C++ objects shared with Lua, they need no interpreter

auto ScriptRuntime::luaSignals() const -> std::vector<std::string> {
	std::vector<std::string> result;
	for (const auto& instance : m_instances) {
		if (!instance) {
			continue;
		}
		for (const auto& [name, signal] : instance->luaSignals()) {
			if (std::ranges::find(result, name) == result.end()) {
				result.push_back(name);
			}
		}
	}
	return result;
}

auto ScriptRuntime::luaSignalConnections(std::string_view name) const -> std::vector<signals::ConnectionInfo> {
	for (const auto& instance : m_instances) {
		if (auto it = instance->luaSignals().find(std::string(name)); it != instance->luaSignals().end()) {
			return it->second.connections();
		}
	}
	return {};
}

auto ScriptRuntime::luaSignalArgTypes(std::string_view name) const -> std::vector<std::string> {
	for (const auto& instance : m_instances) {
		if (auto it = instance->luaSignals().find(std::string(name)); it != instance->luaSignals().end()) {
			return it->second.argTypes();
		}
	}
	return {};
}

auto ScriptRuntime::connectLuaSignal(std::string_view name, toast::Node& target, std::string_view function, bool forwards_args)
    -> bool {
	for (const auto& instance : m_instances) {
		if (auto it = instance->luaSignals().find(std::string(name)); it != instance->luaSignals().end()) {
			return it->second.connect(NodeProxy(target.box()), function, signals::ConnectionSource::editor, forwards_args);
		}
	}
	return false;
}

auto ScriptRuntime::disconnectLuaSignal(std::string_view name, toast::Node& target, std::string_view function) -> bool {
	for (const auto& instance : m_instances) {
		if (auto it = instance->luaSignals().find(std::string(name)); it != instance->luaSignals().end()) {
			return it->second.disconnect(NodeProxy(target.box()), function, signals::ConnectionSource::editor);
		}
	}
	return false;
}

void ScriptRuntime::clearLuaSignal(std::string_view name) {
	for (const auto& instance : m_instances) {
		if (auto it = instance->luaSignals().find(std::string(name)); it != instance->luaSignals().end()) {
			it->second.clear(signals::ConnectionSource::editor);
			return;
		}
	}
}

}
