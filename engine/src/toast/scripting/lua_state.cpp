#include "lua_state.hpp"

#include "asset_proxy.hpp"
#include "lua_callback.hpp"
#include "lua_event.hpp"
#include "lua_signal.hpp"
#include "lua_types.hpp"
#include "lua_util.hpp"
#include "node_proxy.hpp"
#include "script_runtime.hpp"
#include "signal_proxy.hpp"
#include "toast/physics/raycast.hpp"
#include "ui_binds_proxy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iterator>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <memory>
#include <optional>
#include <toast/assets/asset_registry.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/data_schema_codegen.hpp>
#include <toast/engine.hpp>
#include <toast/events/defer.hpp>
#include <toast/input/action.hpp>
#include <toast/log.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/time.hpp>
#include <toast/ui/ui_system.hpp>
#include <toast/voxel/voxel_edit.hpp>
#include <tracy/Tracy.hpp>
#include <tracy/TracyLua.hpp>
#include <utility>

namespace scripting {

namespace {

/// Strip namespace prefix
auto stripNamespace(std::string_view qualified) noexcept -> std::string_view {
	const auto pos = qualified.rfind("::");
	return pos != std::string_view::npos ? qualified.substr(pos + 2) : qualified;
}

auto luaPrint(lua_State* state) -> int {
	int nargs = lua_gettop(state);
	std::string output;

	for (int i = 1; i <= nargs; ++i) {
		if (lua_isstring(state, i)) {
			output += lua_tostring(state, i);
		} else {
			output += luaL_tolstring(state, i, nullptr);
			lua_pop(state, 1);
		}

		if (i < nargs) {
			output += '\t';
		}
	}

	TOAST_INFO("Lua", "{}", output);
	return 0;
}

auto luaWarn(lua_State* state) -> int {
	int nargs = lua_gettop(state);
	std::string output;

	for (int i = 1; i <= nargs; ++i) {
		if (lua_isstring(state, i)) {
			output += lua_tostring(state, i);
		} else {
			output += luaL_tolstring(state, i, nullptr);
			lua_pop(state, 1);
		}

		if (i < nargs) {
			output += '\t';
		}
	}

	TOAST_WARN("Lua", "{}", output);
	return 0;
}

void luaToastTrace(const std::string& msg) {
	TOAST_TRACE("Lua", "{}", msg);
}

void luaToastInfo(const std::string& msg) {
	TOAST_INFO("Lua", "{}", msg);
}

void luaToastWarn(const std::string& msg) {
	TOAST_WARN("Lua", "{}", msg);
}

void luaToastError(const std::string& msg) {
	TOAST_ERROR("Lua", "{}", msg);
}

// Identity of the calling thread and the interpreters it owns right now, one entry per live Lock
std::atomic<uint64_t> g_next_thread_token {1};                                                               // NOLINT
thread_local const uint64_t t_thread_token = g_next_thread_token.fetch_add(1, std::memory_order_relaxed);    // NOLINT
thread_local std::vector<size_t> t_owned_states;                                                             // NOLINT
thread_local int t_non_blocking_depth = 0;                                                                   // NOLINT

auto inputActionValue(const input::Action& action, lua_State* state) -> luabridge::LuaRef {
	const input::Value& value = action.value();
	switch (action.valueType()) {
		case input::ValueType::axis_0d: return {state, value.as<bool>()};
		case input::ValueType::axis_1d: return {state, static_cast<lua_Number>(value.as<float>())};
		case input::ValueType::axis_2d: return {state, value.as<glm::vec2>()};
	}
	return {state};
}

auto inputActionBinds(const input::Action& action, lua_State* state) -> luabridge::LuaRef {
	const auto& binds = action.binds();
	lua_createtable(state, static_cast<int>(binds.size()), 0);
	for (size_t i = 0; i < binds.size(); ++i) {
		lua_pushinteger(state, static_cast<lua_Integer>(i + 1));
		if (auto result = luabridge::Stack<input::Bind>::push(state, binds[i]); !result) {
			lua_pushnil(state);
		}
		lua_settable(state, -3);
	}
	return luabridge::LuaRef::fromStack(state);
}
}

LuaState::Lock::Lock(Entry* entry, lua_State* state, size_t index) noexcept : m_entry(entry), m_state(state), m_index(index) { }

LuaState::Lock::Lock(Lock&& other) noexcept : m_entry(other.m_entry), m_state(other.m_state), m_index(other.m_index) {
	other.m_entry = nullptr;
	other.m_state = nullptr;
}

auto LuaState::Lock::operator=(Lock&& other) noexcept -> Lock& {
	if (this != &other) {
		unlock();
		m_entry = other.m_entry;
		m_state = other.m_state;
		m_index = other.m_index;
		other.m_entry = nullptr;
		other.m_state = nullptr;
	}
	return *this;
}

LuaState::Lock::~Lock() {
	unlock();
}

void LuaState::Lock::unlock() noexcept {
	if (m_entry == nullptr) {
		return;
	}
	LuaState::release(*m_entry, m_index);
	m_entry = nullptr;
	m_state = nullptr;
}

LuaState::NonBlockingScope::NonBlockingScope() noexcept {
	++t_non_blocking_depth;
}

LuaState::NonBlockingScope::~NonBlockingScope() {
	--t_non_blocking_depth;
}

auto LuaState::create() noexcept -> std::unique_ptr<LuaState> {
	TOAST_ASSERT(instance == nullptr, "Lua", "LuaState instance already exists");

	class Helper : public LuaState { };

	return std::make_unique<Helper>();
}

auto LuaState::get() noexcept -> LuaState& {
	TOAST_ASSERT(instance != nullptr, "Lua", "LuaState does not exist");
	return *LuaState::instance;
}

auto LuaState::acquire(size_t index, bool wait) noexcept -> Lock {
	Entry& entry = m_entries[index];
	if (!entry.mutex.try_lock()) {
		if (!wait) {
			return {};
		}
		m_waits.fetch_add(1, std::memory_order_relaxed);
		ZoneScopedN("Lua lock wait");    // NOLINT
		ZoneNameF("Lua lock wait #%d", static_cast<int>(index));
		entry.mutex.lock();
	}

	if (entry.depth++ == 0) {
		entry.owner.store(t_thread_token, std::memory_order_release);
	}
	t_owned_states.push_back(index);
	if (entry.depth == 1) {
		drainPending(entry);
	}
	return Lock(&entry, entry.state, index);
}

void LuaState::release(Entry& entry, size_t index) noexcept {
	if (entry.depth == 1) {
		// Still the owner here, so whatever the destroyers touch is owned
		drainPending(entry);
	}
	for (auto it = t_owned_states.rbegin(); it != t_owned_states.rend(); ++it) {
		if (*it == index) {
			t_owned_states.erase(std::next(it).base());
			break;
		}
	}
	if (--entry.depth == 0) {
		entry.owner.store(0, std::memory_order_release);
	}
	entry.mutex.unlock();
}

void LuaState::drainPending(Entry& entry) noexcept {
	while (entry.pending_count.load(std::memory_order_acquire) != 0) {
		std::vector<std::move_only_function<void()>> batch;
		{
			std::scoped_lock lock(entry.pending_mutex);
			batch.swap(entry.pending);
			entry.pending_count.store(0, std::memory_order_release);
		}
		for (auto& destroyer : batch) {
			if (destroyer) {
				destroyer();
			}
		}
	}
}

auto LuaState::lock(size_t index) noexcept -> Lock {
	if (index >= m_entries.size()) {
		return {};
	}
	// Waiting is only safe for a thread that owns nothing, and recursion on an owned interpreter never waits
	if ((t_owned_states.empty() && t_non_blocking_depth == 0) || ownedByCurrentThread(index)) {
		return acquire(index, true);
	}
	Lock result = acquire(index, false);
	if (!result) {
		m_contention.fetch_add(1, std::memory_order_relaxed);
	}
	return result;
}

auto LuaState::tryLock(size_t index) noexcept -> Lock {
	if (index >= m_entries.size()) {
		return {};
	}
	return acquire(index, false);
}

auto LuaState::indexOf(lua_State* state) noexcept -> std::optional<size_t> {
	if (state == nullptr || !LuaState::exists()) {
		return std::nullopt;
	}
	// The pool tags the main thread of every interpreter and new coroutines inherit the tag
	const auto tag = *static_cast<const uintptr_t*>(lua_getextraspace(state));
	if (tag == 0 || tag > instance->m_entries.size()) {
		return std::nullopt;
	}
	return static_cast<size_t>(tag - 1);
}

auto LuaState::ownedByCurrentThread(size_t index) noexcept -> bool {
	return std::ranges::find(t_owned_states, index) != t_owned_states.end();
}

auto LuaState::ownedByCurrentThread(lua_State* state) noexcept -> bool {
	const auto index = indexOf(state);
	return index.has_value() && ownedByCurrentThread(*index);
}

auto LuaState::ownsAnyState() noexcept -> bool {
	return !t_owned_states.empty();
}

auto LuaState::lastOwnedIndex() noexcept -> std::optional<size_t> {
	if (t_owned_states.empty()) {
		return std::nullopt;
	}
	return t_owned_states.back();
}

auto LuaState::mainState(size_t index) const noexcept -> lua_State* {
	return index < m_entries.size() ? m_entries[index].state : nullptr;
}

void LuaState::retire(size_t index, std::move_only_function<void()> destroyer) noexcept {
	if (!destroyer || index >= m_entries.size()) {
		return;
	}
	// Free, or already owned by this thread; either way it can run right here
	if (Lock guard = acquire(index, false)) {
		destroyer();
		return;
	}
	Entry& entry = m_entries[index];
	std::scoped_lock lock(entry.pending_mutex);
	entry.pending.push_back(std::move(destroyer));
	entry.pending_count.fetch_add(1, std::memory_order_release);
}

void LuaState::retireOrLeak(size_t index, std::move_only_function<void()> destroyer) noexcept {
	if (LuaState::exists()) {
		LuaState::get().retire(index, std::move(destroyer));
		return;
	}
	// The interpreters are gone and whatever the destroyer holds points into them, so it must never run
	new std::move_only_function<void()>(std::move(destroyer));    // NOLINT(cppcoreguidelines-owning-memory)
}

auto LuaState::assign(uint64_t group, std::optional<size_t> forced) -> size_t {
	std::scoped_lock lock(m_placement_mutex);

	// Equally loaded interpreters take turns, because the search starts at a different one every time
	auto least_loaded = [this](auto&& has_room) {
		auto search = [this](auto&& usable) {
			std::optional<size_t> best;
			for (size_t step = 0; step < m_pool_size; ++step) {
				const size_t candidate = (m_next_tie + step) % m_pool_size;
				if (usable(candidate) && (!best.has_value() || m_loads[candidate] < m_loads[*best])) {
					best = candidate;
				}
			}
			return best;
		};
		++m_next_tie;
		if (const auto with_room = search(has_room)) {
			return *with_room;
		}
		return *search([](size_t) { return true; });
	};

	GroupPlacement* placement = group != 0 ? &m_groups[group] : nullptr;
	if (placement != nullptr && placement->per_index.empty()) {
		placement->per_index.assign(m_pool_size, 0);
	}

	size_t index = 0;
	if (forced.has_value() && *forced < m_pool_size) {
		index = *forced;
	} else if (placement == nullptr) {
		index = least_loaded([](size_t) { return true; });
	} else {
		if (placement->members == 0 || placement->per_index[placement->index] >= k_group_chunk) {
			placement->index = least_loaded([placement](size_t candidate) { return placement->per_index[candidate] < k_group_chunk; });
		}
		index = placement->index;
	}
	if (placement != nullptr) {
		++placement->per_index[index];
		++placement->members;
	}
	++m_loads[index];
	return index;
}

auto LuaState::loads() const -> std::vector<uint32_t> {
	std::scoped_lock lock(m_placement_mutex);
	return m_loads;
}

void LuaState::unassign(uint64_t group, size_t index) noexcept {
	std::scoped_lock lock(m_placement_mutex);
	if (index < m_loads.size() && m_loads[index] > 0) {
		--m_loads[index];
	}
	if (group == 0) {
		return;
	}
	const auto found = m_groups.find(group);
	if (found == m_groups.end()) {
		return;
	}
	// A runtime that is rebuilt (a script reload) gives its place back, so its replacement lands next to the others
	if (index < found->second.per_index.size() && found->second.per_index[index] > 0) {
		--found->second.per_index[index];
	}
	if (found->second.members > 0 && --found->second.members == 0) {
		m_groups.erase(found);
	}
}

auto LuaState::newGroup() noexcept -> uint64_t {
	static std::atomic<uint64_t> s_next_group {1};    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
	return s_next_group.fetch_add(1, std::memory_order_relaxed);
}

auto LuaState::runString(std::string_view lua_code) noexcept -> bool {
	return runStringOn(0, lua_code, nullptr);
}

auto LuaState::runStringOn(size_t index, std::string_view lua_code, std::string* error) noexcept -> bool {
	ZoneScoped;
	Lock guard = lock(index);
	if (!guard) {
		if (error != nullptr) {
			*error = "the interpreter is busy";
		}
		return false;
	}
	lua_State* state = guard.state();
	const int top = lua_gettop(state);

	int load_status = luaL_loadbufferx(state, lua_code.data(), lua_code.size(), "=runString", nullptr);
	if (load_status != LUA_OK) {
		TOAST_ERROR("Lua", "Failed to load Lua code: {}", lua_tostring(state, -1));
		if (error != nullptr) {
			*error = lua_tostring(state, -1);
		}
		lua_settop(state, top);
		return false;
	}

	int pcall_status = pcallTraceback(state, 0, LUA_MULTRET);
	if (pcall_status != LUA_OK) {
		TOAST_ERROR("Lua", "Failed to execute Lua code: {}", lua_tostring(state, -1));
		if (error != nullptr) {
			*error = lua_tostring(state, -1);
		}
		lua_settop(state, top);
		return false;
	}

	lua_settop(state, top);    // the chunk's results are not wanted, and they would pile up on the main stack
	return true;
}

LuaState::LuaState() : m_pool_size(1 + toast::ThreadPool::workerCount()), m_entries(m_pool_size), m_loads(m_pool_size, 0) {
	ZoneScoped;
	LuaState::instance = this;

	for (size_t i = 0; i < m_pool_size; ++i) {
		Entry& entry = m_entries[i];
		entry.state = luaL_newstate();
		TOAST_ASSERT(entry.state != nullptr, "Lua", "Failed to create Lua state");
		// Every coroutine created later inherits this tag, which is how a thread is traced back to its interpreter
		*static_cast<uintptr_t*>(lua_getextraspace(entry.state)) = static_cast<uintptr_t>(i + 1);
		luaL_openlibs(entry.state);

		lua_atpanic(entry.state, [](auto* state) -> int {
			const char* message = lua_tostring(state, -1);
			luaL_traceback(state, state, message ? message : "(non-string error)", 0);
			TOAST_ERROR("Lua", "Panic: {}\n{}", message ? message : "(non-string error)", lua_tostring(state, -1));
			return 0;
		});

		luabridge::getGlobalNamespace(entry.state).addFunction("print", luaPrint).addFunction("warn", luaWarn);

		// Tracy's lua-side profiling API (tracy.ZoneBegin/ZoneBeginN/ZoneEnd/Message);
		// registers no-op stubs when TRACY_ENABLE is off
		tracy::LuaRegister(entry.state);

		registerApi(entry.state);
	}

	TOAST_INFO("Lua", "Created pool of {} lua states", m_pool_size);
}

LuaState::~LuaState() noexcept {
	clearAllLuaEventSubscriptions();
	for (size_t i = 0; i < m_entries.size(); ++i) {
		// Destroyers queued by threads that never got the interpreter have to run while it still exists
		{ Lock guard = acquire(i, true); }
		lua_close(m_entries[i].state);
	}
	LuaState::instance = nullptr;
	TOAST_INFO("Lua", "Destroyed lua state pool");
}

void LuaState::registerApi(lua_State* state) noexcept {
	ZoneScoped;
	using namespace luabridge;

	getGlobalNamespace(state)
	    .addFunction(
	        "defer",
	        +[](const luabridge::LuaRef& fn, lua_State* state) {
		        if (!fn.isFunction()) {
			        luaL_error(state, "defer expects a function as its argument");
			        return;
		        }

		        LuaCallback callback = LuaCallback::capture(fn, "deferred function");
		        if (!callback) {
			        luaL_error(state, "defer: the function does not belong to a script interpreter");
			        return;
		        }
		        toast::defer([callback]() { (void)callback.invoke(); });
	        }
	    )
	    .beginNamespace("toast")
	    .addFunction("trace", luaToastTrace)
	    .addFunction("info", luaToastInfo)
	    .addFunction("warn", luaToastWarn)
	    .addFunction("error", luaToastError)

	    .addFunction(
	        "load", +[](const std::string& path) -> AssetProxy { return AssetProxy(assets::load(path)); }
	    )
	    .endNamespace()

	    .beginNamespace("event")
	    .addFunction(
	        "send",
	        +[](const LuaEventDescriptor& descriptor, const luabridge::LuaRef& payload, lua_State* state) {
		        auto binding = LuaEventRegistry::find(descriptor.name);
		        if (!binding) {
			        luaL_error(state, "event.send: unknown event descriptor '%s'", descriptor.name.c_str());
			        return;
		        }
		        if (!binding->send) {
			        luaL_error(state, "event.send: '%s' is receive-only", descriptor.name.c_str());
			        return;
		        }
		        if (!payload.isTable()) {
			        luaL_error(state, "event.send: payload must be a table");
			        return;
		        }
		        payload.push(state);
		        const int table = lua_absindex(state, -1);
		        std::string error;
		        const bool sent = binding->send(state, table, error);
		        lua_pop(state, 1);
		        if (!sent) {
			        luaL_error(state, "event.send(%s): %s", descriptor.name.c_str(), error.c_str());
		        }
	        }
	    )
	    .endNamespace()

	    // vec2
	    .beginClass<glm::vec2>("vec2")
	    .addConstructor<void (*)(float, float)>()
	    .addProperty("x", &glm::vec2::x)
	    .addProperty("y", &glm::vec2::y)
	    // Arithmetic metamethods
	    .addFunction("__add", [](const glm::vec2& a, const glm::vec2& b) { return a + b; })
	    .addFunction("__sub", [](const glm::vec2& a, const glm::vec2& b) { return a - b; })
	    .addFunction(
	        "__mul",
	        overload<const glm::vec2&, float>(+[](const glm::vec2& v, float s) { return v * s; }),
	        overload<const glm::vec2&, const glm::vec2&>(+[](const glm::vec2& a, const glm::vec2& b) { return a * b; })
	    )
	    .addFunction(
	        "__div",
	        overload<const glm::vec2&, float>(+[](const glm::vec2& v, float s) { return v / s; }),
	        overload<const glm::vec2&, const glm::vec2&>(+[](const glm::vec2& a, const glm::vec2& b) { return a / b; })
	    )
	    .addFunction("__unm", [](const glm::vec2& v, const glm::vec2&) { return -v; })
	    .addFunction("__eq", [](const glm::vec2& a, const glm::vec2& b) { return a == b; })
	    .addFunction("__len", [](const glm::vec2& v) { return glm::length(v); })
	    .addFunction("__tostring", [](const glm::vec2& v) -> std::string { return std::format("vec2({}, {})", v.x, v.y); })
	    // Methods
	    .addFunction("length", [](const glm::vec2& v) { return glm::length(v); })
	    .addFunction("length2", [](const glm::vec2& v) { return glm::dot(v, v); })
	    .addFunction("normalize", [](const glm::vec2& v) { return glm::normalize(v); })
	    .addFunction("dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); })
	    .addFunction("distance", [](const glm::vec2& a, const glm::vec2& b) { return glm::distance(a, b); })
	    .addFunction("lerp", [](const glm::vec2& a, const glm::vec2& b, float t) { return glm::mix(a, b, t); })
	    .addFunction("reflect", [](const glm::vec2& v, const glm::vec2& n) { return glm::reflect(v, n); })
	    .addFunction(
	        "project", [](const glm::vec2& v, const glm::vec2& onto) { return (glm::dot(v, onto) / glm::dot(onto, onto)) * onto; }
	    )
	    .addFunction(
	        "angle",
	        [](const glm::vec2& a, const glm::vec2& b) {
		        float d = glm::dot(glm::normalize(a), glm::normalize(b));
		        return std::acos(std::clamp(d, -1.0f, 1.0f));
	        }
	    )
	    .addFunction("abs", [](const glm::vec2& v) { return glm::abs(v); })
	    .addFunction(
	        "clamp",
	        overload<const glm::vec2&, float, float>(+[](const glm::vec2& v, float lo, float hi) { return glm::clamp(v, lo, hi); }),
	        overload<const glm::vec2&, const glm::vec2&, const glm::vec2&>(
	            +[](const glm::vec2& v, const glm::vec2& lo, const glm::vec2& hi) { return glm::clamp(v, lo, hi); }
	        )
	    )
	    .addFunction("min", [](const glm::vec2& a, const glm::vec2& b) { return glm::min(a, b); })
	    .addFunction("max", [](const glm::vec2& a, const glm::vec2& b) { return glm::max(a, b); })
	    .endClass()

	    // vec3
	    .beginClass<glm::vec3>("vec3")
	    .addConstructor<void (*)(float, float, float)>()
	    .addProperty("x", &glm::vec3::x)
	    .addProperty("y", &glm::vec3::y)
	    .addProperty("z", &glm::vec3::z)
	    .addProperty("r", &glm::vec3::x)
	    .addProperty("g", &glm::vec3::y)
	    .addProperty("b", &glm::vec3::z)
	    // Arithmetic metamethods
	    .addFunction("__add", [](const glm::vec3& a, const glm::vec3& b) { return a + b; })
	    .addFunction("__sub", [](const glm::vec3& a, const glm::vec3& b) { return a - b; })
	    .addFunction(
	        "__mul",
	        overload<const glm::vec3&, float>(+[](const glm::vec3& v, float s) { return v * s; }),
	        overload<const glm::vec3&, const glm::vec3&>(+[](const glm::vec3& a, const glm::vec3& b) { return a * b; })
	    )
	    .addFunction(
	        "__div",
	        overload<const glm::vec3&, float>(+[](const glm::vec3& v, float s) { return v / s; }),
	        overload<const glm::vec3&, const glm::vec3&>(+[](const glm::vec3& a, const glm::vec3& b) { return a / b; })
	    )
	    .addFunction("__unm", [](const glm::vec3& v, const glm::vec3&) { return -v; })
	    .addFunction("__eq", [](const glm::vec3& a, const glm::vec3& b) { return a == b; })
	    .addFunction("__len", [](const glm::vec3& v) { return glm::length(v); })
	    .addFunction("__tostring", [](const glm::vec3& v) -> std::string { return std::format("vec3({}, {}, {})", v.x, v.y, v.z); })
	    // Methods
	    .addFunction("length", [](const glm::vec3& v) { return glm::length(v); })
	    .addFunction("length2", [](const glm::vec3& v) { return glm::dot(v, v); })
	    .addFunction("normalize", [](const glm::vec3& v) { return glm::normalize(v); })
	    .addFunction("dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); })
	    .addFunction("cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); })
	    .addFunction("distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); })
	    .addFunction("lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); })
	    .addFunction("reflect", [](const glm::vec3& v, const glm::vec3& n) { return glm::reflect(v, n); })
	    .addFunction(
	        "project", [](const glm::vec3& v, const glm::vec3& onto) { return (glm::dot(v, onto) / glm::dot(onto, onto)) * onto; }
	    )
	    .addFunction(
	        "angle",
	        [](const glm::vec3& a, const glm::vec3& b) {
		        float d = glm::dot(glm::normalize(a), glm::normalize(b));
		        return std::acos(std::clamp(d, -1.0f, 1.0f));
	        }
	    )
	    .addFunction("abs", [](const glm::vec3& v) { return glm::abs(v); })
	    .addFunction(
	        "clamp",
	        overload<const glm::vec3&, float, float>(+[](const glm::vec3& v, float lo, float hi) { return glm::clamp(v, lo, hi); }),
	        overload<const glm::vec3&, const glm::vec3&, const glm::vec3&>(
	            +[](const glm::vec3& v, const glm::vec3& lo, const glm::vec3& hi) { return glm::clamp(v, lo, hi); }
	        )
	    )
	    .addFunction("min", [](const glm::vec3& a, const glm::vec3& b) { return glm::min(a, b); })
	    .addFunction("max", [](const glm::vec3& a, const glm::vec3& b) { return glm::max(a, b); })
	    .endClass()

	    .deriveClass<Vec3FieldProxy, glm::vec3>("__field_vec3")
	    .addProperty("x", &Vec3FieldProxy::getX, &Vec3FieldProxy::setX)
	    .addProperty("y", &Vec3FieldProxy::getY, &Vec3FieldProxy::setY)
	    .addProperty("z", &Vec3FieldProxy::getZ, &Vec3FieldProxy::setZ)
	    .addProperty("r", &Vec3FieldProxy::getX, &Vec3FieldProxy::setX)
	    .addProperty("g", &Vec3FieldProxy::getY, &Vec3FieldProxy::setY)
	    .addProperty("b", &Vec3FieldProxy::getZ, &Vec3FieldProxy::setZ)
	    .endClass()

	    // vec4
	    .beginClass<glm::vec4>("vec4")
	    .addConstructor<void (*)(float, float, float, float)>()
	    .addProperty("x", &glm::vec4::x)
	    .addProperty("y", &glm::vec4::y)
	    .addProperty("z", &glm::vec4::z)
	    .addProperty("w", &glm::vec4::w)
	    .addProperty("r", &glm::vec4::x)
	    .addProperty("g", &glm::vec4::y)
	    .addProperty("b", &glm::vec4::z)
	    .addProperty("a", &glm::vec4::w)
	    // Arithmetic metamethods
	    .addFunction("__add", [](const glm::vec4& a, const glm::vec4& b) { return a + b; })
	    .addFunction("__sub", [](const glm::vec4& a, const glm::vec4& b) { return a - b; })
	    .addFunction(
	        "__mul",
	        overload<const glm::vec4&, float>(+[](const glm::vec4& v, float s) { return v * s; }),
	        overload<const glm::vec4&, const glm::vec4&>(+[](const glm::vec4& a, const glm::vec4& b) { return a * b; })
	    )
	    .addFunction(
	        "__div",
	        overload<const glm::vec4&, float>(+[](const glm::vec4& v, float s) { return v / s; }),
	        overload<const glm::vec4&, const glm::vec4&>(+[](const glm::vec4& a, const glm::vec4& b) { return a / b; })
	    )
	    .addFunction("__unm", [](const glm::vec4& v, const glm::vec4&) { return -v; })
	    .addFunction("__eq", [](const glm::vec4& a, const glm::vec4& b) { return a == b; })
	    .addFunction("__len", [](const glm::vec4& v) { return glm::length(v); })
	    .addFunction(
	        "__tostring", [](const glm::vec4& v) -> std::string { return std::format("vec4({}, {}, {}, {})", v.x, v.y, v.z, v.w); }
	    )
	    // Methods
	    .addFunction("length", [](const glm::vec4& v) { return glm::length(v); })
	    .addFunction("length2", [](const glm::vec4& v) { return glm::dot(v, v); })
	    .addFunction("normalize", [](const glm::vec4& v) { return glm::normalize(v); })
	    .addFunction("dot", [](const glm::vec4& a, const glm::vec4& b) { return glm::dot(a, b); })
	    .addFunction("distance", [](const glm::vec4& a, const glm::vec4& b) { return glm::distance(a, b); })
	    .addFunction("lerp", [](const glm::vec4& a, const glm::vec4& b, float t) { return glm::mix(a, b, t); })
	    .addFunction("abs", [](const glm::vec4& v) { return glm::abs(v); })
	    .addFunction(
	        "clamp",
	        overload<const glm::vec4&, float, float>(+[](const glm::vec4& v, float lo, float hi) { return glm::clamp(v, lo, hi); }),
	        overload<const glm::vec4&, const glm::vec4&, const glm::vec4&>(
	            +[](const glm::vec4& v, const glm::vec4& lo, const glm::vec4& hi) { return glm::clamp(v, lo, hi); }
	        )
	    )
	    .addFunction("min", [](const glm::vec4& a, const glm::vec4& b) { return glm::min(a, b); })
	    .addFunction("max", [](const glm::vec4& a, const glm::vec4& b) { return glm::max(a, b); })
	    .endClass()

	    // quat
	    .beginClass<glm::quat>("quat")
	    .addProperty("x", &glm::quat::x)
	    .addProperty("y", &glm::quat::y)
	    .addProperty("z", &glm::quat::z)
	    .addProperty("w", &glm::quat::w)
	    .addStaticFunction(
	        "new", +[](float x, float y, float z, float w) { return glm::quat(w, x, y, z); }
	    )
	    .addStaticFunction(
	        "identity", +[]() { return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); }
	    )
	    .addStaticFunction(
	        "fromEuler", +[](const glm::vec3& degrees) { return glm::quat(glm::radians(degrees)); }
	    )
	    .addStaticFunction(
	        "angleAxis",
	        +[](float degrees, const glm::vec3& axis) { return glm::angleAxis(glm::radians(degrees), glm::normalize(axis)); }
	    )
	    .addFunction("toEuler", [](const glm::quat& q) { return glm::degrees(glm::eulerAngles(q)); })
	    .addFunction("normalize", [](const glm::quat& q) { return glm::normalize(q); })
	    .addFunction("inverse", [](const glm::quat& q) { return glm::inverse(q); })
	    .addFunction("slerp", [](const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); })
	    .addFunction(
	        "__mul",
	        overload<const glm::quat&, const glm::quat&>(+[](const glm::quat& a, const glm::quat& b) { return a * b; }),
	        overload<const glm::quat&, const glm::vec3&>(+[](const glm::quat& q, const glm::vec3& v) { return q * v; })
	    )
	    .addFunction("__eq", [](const glm::quat& a, const glm::quat& b) { return a == b; })
	    .addFunction(
	        "__tostring", [](const glm::quat& q) -> std::string { return std::format("quat({}, {}, {}, {})", q.x, q.y, q.z, q.w); }
	    )
	    .endClass()

	    // colors
	    .beginClass<Color3>("color3")
	    .addConstructor<void (*)(float, float, float)>()
	    .addProperty(
	        "r", +[](const Color3* c) { return c->rgb.r; }, +[](Color3* c, float v) { c->rgb.r = v; }
	    )
	    .addProperty(
	        "g", +[](const Color3* c) { return c->rgb.g; }, +[](Color3* c, float v) { c->rgb.g = v; }
	    )
	    .addProperty(
	        "b", +[](const Color3* c) { return c->rgb.b; }, +[](Color3* c, float v) { c->rgb.b = v; }
	    )
	    .addFunction("vec3", [](const Color3& c) { return c.rgb; })
	    .addFunction("__tostring", &Color3::toString)
	    .endClass()

	    .beginClass<Color4>("color4")
	    .addConstructor<void (*)(float, float, float, float)>()
	    .addProperty(
	        "r", +[](const Color4* c) { return c->rgba.r; }, +[](Color4* c, float v) { c->rgba.r = v; }
	    )
	    .addProperty(
	        "g", +[](const Color4* c) { return c->rgba.g; }, +[](Color4* c, float v) { c->rgba.g = v; }
	    )
	    .addProperty(
	        "b", +[](const Color4* c) { return c->rgba.b; }, +[](Color4* c, float v) { c->rgba.b = v; }
	    )
	    .addProperty(
	        "a", +[](const Color4* c) { return c->rgba.a; }, +[](Color4* c, float v) { c->rgba.a = v; }
	    )
	    .addFunction("vec4", [](const Color4& c) { return c.rgba; })
	    .addFunction("__tostring", &Color4::toString)
	    .endClass()

	    .beginClass<input::KeyCode>("InputKeyCode")
	    .addProperty(
	        "device", +[](const input::KeyCode* key) { return static_cast<lua_Integer>(key->device); }
	    )
	    .addProperty(
	        "kind", +[](const input::KeyCode* key) { return static_cast<lua_Integer>(key->kind); }
	    )
	    .addProperty(
	        "code", +[](const input::KeyCode* key) { return key->code; }
	    )
	    .addProperty(
	        "valid", +[](const input::KeyCode* key) { return key->valid; }
	    )
	    .endClass()

	    .beginClass<input::Bind>("InputBind")
	    .addFunction(
	        "keycode", +[](const input::Bind& bind) { return bind.keycode(); }
	    )
	    .addFunction(
	        "keycodeString", +[](const input::Bind& bind) { return std::string(bind.keycodeString()); }
	    )
	    .endClass()

	    .beginClass<input::Action>("InputAction")
	    .addFunction(
	        "uid", +[](const input::Action& action) { return static_cast<lua_Integer>(action.uid().data()); }
	    )
	    .addFunction(
	        "name", +[](const input::Action& action) { return std::string(action.name()); }
	    )
	    .addFunction(
	        "functionName", +[](const input::Action& action) { return std::string(action.functionName()); }
	    )
	    .addFunction(
	        "valueType", +[](const input::Action& action) { return static_cast<lua_Integer>(action.valueType()); }
	    )
	    .addFunction("value", inputActionValue)
	    .addFunction(
	        "modifiers", +[](const input::Action& action) { return static_cast<lua_Integer>(action.modifiers()); }
	    )
	    .addFunction(
	        "device", +[](const input::Action& action) { return static_cast<lua_Integer>(action.device()); }
	    )
	    .addFunction("timeSinceStart", &input::Action::timeSinceStart)
	    .addFunction("timeSinceTry", &input::Action::timeSinceTry)
	    .addFunction("remainingCountdown", &input::Action::remainingCountdown)
	    .addFunction("binds", inputActionBinds)
	    .addFunction(
	        "__tostring", +[](const input::Action& action) { return std::format("InputAction({})", action.name()); }
	    )
	    .endClass()

	    .beginClass<physics::RayHit>("RayHit")
	    .addProperty("position", &physics::RayHit::position)
	    .addProperty("normal", &physics::RayHit::normal)
	    .addProperty("distance", &physics::RayHit::distance)
	    .addProperty(
	        "node", +[](const physics::RayHit* hit) { return NodeProxy(hit->node); }
	    )
	    .addFunction(
	        "__tostring",
	        [](const physics::RayHit& hit) -> std::string {
		        return std::format(
		            "RayHit(distance: {}, pos: vec3({}, {}, {}))", hit.distance, hit.position.x, hit.position.y, hit.position.z
		        );
	        }
	    )
	    .endClass()

	    .beginNamespace("InputEvent")
	    .addVariable("start", input::ActionEvent::start)
	    .addVariable("hold", input::ActionEvent::hold)
	    .addVariable("release", input::ActionEvent::release)
	    .addVariable("tries", input::ActionEvent::tries)
	    .addVariable("countdown", input::ActionEvent::countdown)
	    .addVariable("cancelled", input::ActionEvent::cancelled)
	    .endNamespace()

	    .beginNamespace("InputDevice")
	    .addVariable("none", input::Device::none)
	    .addVariable("keyboard", input::Device::keyboard)
	    .addVariable("mouse", input::Device::mouse)
	    .addVariable("controller", input::Device::controller)
	    .endNamespace()

	    .beginNamespace("InputValueType")
	    .addVariable("axis0d", input::ValueType::axis_0d)
	    .addVariable("axis1d", input::ValueType::axis_1d)
	    .addVariable("axis2d", input::ValueType::axis_2d)
	    .endNamespace()

	    .beginNamespace("InputModifier")
	    .addVariable("none", input::ModifierKey::none)
	    .addVariable("shift", input::ModifierKey::shift)
	    .addVariable("control", input::ModifierKey::control)
	    .addVariable("alt", input::ModifierKey::alt)
	    .endNamespace()

	    .beginNamespace("InputKind")
	    .addVariable("button", input::InputKind::button)
	    .addVariable("axis1d", input::InputKind::axis1d)
	    .addVariable("axis2d", input::InputKind::axis2d)
	    .addVariable("scroll", input::InputKind::scroll)
	    .addVariable("cursor", input::InputKind::cursor)
	    .endNamespace()

	    .beginNamespace("VoxelWrite")
	    .addVariable("Replace", voxel::WriteMode::replace)
	    .addVariable("EmptyOnly", voxel::WriteMode::empty_only)
	    .addVariable("SolidOnly", voxel::WriteMode::solid_only)
	    .addVariable("Match", voxel::WriteMode::match)
	    .endNamespace()

	    // AssetProxy
	    .beginClass<AssetProxy>("Asset")
	    .addFunction("path", &AssetProxy::path)
	    .addFunction("uid", [](const AssetProxy& a) { return static_cast<lua_Integer>(a.uid().data()); })
	    .addFunction("hasValue", &AssetProxy::hasValue)
	    .addFunction("type", &AssetProxy::type)
	    .addFunction("get", &AssetProxy::get)
	    .addFunction("__tostring", &AssetProxy::toString)
	    .addIndexMetaMethod(assetProxyIndex)
	    .addNewIndexMetaMethod(assetProxyNewindex)
	    .endClass()

	    // NodeProxy
	    .beginClass<NodeProxy>("Node")
	    .addFunction("exists", &NodeProxy::exists)
	    .addFunction("name", &NodeProxy::name)
	    .addFunction("uid", [](const NodeProxy& np) { return static_cast<lua_Integer>(np.uid()); })
	    .addFunction("find", &NodeProxy::find)
	    .addFunction("search", &NodeProxy::search)
	    .addFunction("parent", &NodeProxy::parent)
	    .addFunction("root", &NodeProxy::root)
	    .addFunction(
	        "searchType",
	        overload<const TypeMarker&, lua_State*>(&NodeProxy::searchType),
	        overload<const std::string&, lua_State*>(&NodeProxy::searchType)
	    )
	    .addFunction(
	        "getChildren",
	        overload<lua_State*>(&NodeProxy::getChildren),
	        overload<const TypeMarker&, lua_State*>(&NodeProxy::getChildren),
	        overload<const std::string&, lua_State*>(&NodeProxy::getChildren)
	    )
	    .addFunction("create", &NodeProxy::create)
	    .addFunction("addDependsOn", &NodeProxy::addDependsOn)
	    .addFunction("interactsWith", &NodeProxy::interactsWith)
	    .addFunction("call", &NodeProxy::call)
	    .addIndexMetaMethod(nodeProxyIndex)
	    .addNewIndexMetaMethod(nodeProxyNewindex)
	    .endClass()

	    .beginClass<LuaEventDescriptor>("EventDescriptor")
	    .addProperty("name", &LuaEventDescriptor::name)
	    .endClass()

	    .beginClass<ListenerProxy>("EventListener")
	    .addFunction("subscribe", &ListenerProxy::subscribe)
	    .addFunction(
	        "unsubscribe",
	        overload<const LuaEventDescriptor&>(&ListenerProxy::unsubscribe),
	        overload<const LuaEventDescriptor&, const std::string&>(&ListenerProxy::unsubscribe)
	    )
	    .endClass()

	    // SignalProxy
	    .beginClass<SignalProxy>("Signal")
	    .addFunction(
	        "connect",
	        overload<SignalProxy&, const NodeProxy&, const std::string&>(
	            +[](SignalProxy& signal, const NodeProxy& target, const std::string& function) {
		            return signal.connect(target, function, signals::ConnectionSource::lua, true);
	            }
	        ),
	        overload<SignalProxy&, const NodeProxy&, const std::string&, bool>(
	            +[](SignalProxy& signal, const NodeProxy& target, const std::string& function, bool forwards_args) {
		            return signal.connect(target, function, signals::ConnectionSource::lua, forwards_args);
	            }
	        ),
	        overload<SignalProxy&, const luabridge::LuaRef&, const std::string&>(
	            +[](SignalProxy& signal, const luabridge::LuaRef& target, const std::string& function) {
		            if (const NodeProxy node = nodeOfSelfTable(target); node.exists()) {
			            return signal.connect(node, function, signals::ConnectionSource::lua, true);
		            }
		            return target.isTable() && signal.connectSelf(function, signals::ConnectionSource::lua);
	            }
	        ),
	        overload<SignalProxy&, const luabridge::LuaRef&, const std::string&, bool>(
	            +[](SignalProxy& signal, const luabridge::LuaRef& target, const std::string& function, bool forwards_args) {
		            if (const NodeProxy node = nodeOfSelfTable(target); node.exists()) {
			            return signal.connect(node, function, signals::ConnectionSource::lua, forwards_args);
		            }
		            return target.isTable() && signal.connectSelf(function, signals::ConnectionSource::lua, forwards_args);
	            }
	        )
	    )
	    .addFunction(
	        "disconnect",
	        [](SignalProxy& signal, const NodeProxy& target, const std::string& function) {
		        return signal.disconnect(target, signals::ConnectionSource::lua, function);
	        },
	        [](SignalProxy& signal, const luabridge::LuaRef& target, const std::string& function) {
		        if (const NodeProxy node = nodeOfSelfTable(target); node.exists()) {
			        return signal.disconnect(node, signals::ConnectionSource::lua, function);
		        }
		        return target.isTable() && signal.disconnectSelf(signals::ConnectionSource::lua, function);
	        }
	    )
	    .addFunction("clear", [](SignalProxy& signal) { signal.clear(signals::ConnectionSource::lua); })
	    .addFunction("fire", &SignalProxy::fire)
	    .endClass()

	    .beginClass<LuaSignal>("LuaSignal")
	    .addFunction(
	        "connect",
	        [](LuaSignal& signal, const NodeProxy& target, const std::string& function) {
		        return signal.connect(target, function, signals::ConnectionSource::lua);
	        },
	        [](LuaSignal& signal, const luabridge::LuaRef& target, const std::string& function) {
		        if (const NodeProxy node = nodeOfSelfTable(target); node.exists()) {
			        return signal.connect(node, function, signals::ConnectionSource::lua);
		        }
		        return target.isTable() && signal.connectSelf(function, signals::ConnectionSource::lua);
	        }
	    )
	    .addFunction(
	        "disconnect",
	        [](LuaSignal& signal, const NodeProxy& target, const std::string& function) {
		        return signal.disconnect(target, function, signals::ConnectionSource::lua);
	        },
	        [](LuaSignal& signal, const luabridge::LuaRef& target, const std::string& function) {
		        if (const NodeProxy node = nodeOfSelfTable(target); node.exists()) {
			        return signal.disconnect(node, function, signals::ConnectionSource::lua);
		        }
		        return target.isTable() && signal.disconnectSelf(function, signals::ConnectionSource::lua);
	        }
	    )
	    .addFunction("clear", [](LuaSignal& signal) { signal.clear(signals::ConnectionSource::lua); })
	    .addFunction(
	        "fire",
	        +[](LuaSignal& signal, lua_State* state) {
		        signals::DynamicArgs args;
		        const int top = lua_gettop(state);
		        for (int i = 2; i <= top; ++i) {
			        args.values.push_back(luaRefValueToAny(state, luabridge::LuaRef::fromStack(state, i)));
		        }
		        signal.fire(std::move(args));
	        }
	    )
	    .endClass()

	    .beginNamespace("Signal")
	    .addFunction(
	        "create",
	        +[](lua_State* state) {
		        LuaSignal signal;
		        std::vector<std::string> types;
		        const int top = lua_gettop(state);
		        for (int i = 1; i <= top; ++i) {
			        if (lua_type(state, i) == LUA_TSTRING) {
				        types.emplace_back(lua_tostring(state, i));
			        }
		        }
		        signal.argTypes(std::move(types));
		        return signal;
	        }
	    )
	    .endNamespace()

	    // UIBindsProxy
	    .beginClass<UIBindsProxy>("UIBinds")
	    .addIndexMetaMethod(uiBindsProxyIndex)
	    .addNewIndexMetaMethod(uiBindsProxyNewindex)
	    .endClass()

	    // TypeMarker
	    .beginClass<TypeMarker>("_TypeMarker")
	    .addFunction("__tostring", &TypeMarker::toString)
	    .endClass()

	    // Time
	    .beginNamespace("Time")
	    .addFunction(
	        "delta", +[]() -> double { return Time::delta(); }
	    )
	    .addFunction(
	        "rawDelta", +[]() -> double { return Time::rawDelta(); }
	    )
	    .addFunction(
	        "renderDelta", +[]() -> double { return Time::renderDelta(); }
	    )
	    .addFunction(
	        "frame", +[]() -> uint64_t { return Time::frame(); }
	    )
	    .addFunction(
	        "uptime", +[]() -> double { return Time::uptime(); }
	    )
	    .addFunction(
	        "fps", +[]() -> double { return Time::fps(); }
	    )
	    .addFunction(
	        "tps", +[]() -> double { return Time::tps(); }
	    )
	    .addFunction("scale", overload<>(+[]() -> double { return Time::scale(); }), overload<double>(+[](double v) {
		                 Time::scale(v);
	                 }))
	    .addFunction(
	        "paused", +[]() -> bool { return Time::paused(); }
	    )
	    .addFunction(
	        "pause", +[]() { Time::pause(); }
	    )
	    .addFunction(
	        "resume", +[]() { Time::resume(); }
	    )
	    .endNamespace()

	    .beginNamespace("UI")
	    .addFunction(
	        "setLanguage",
	        +[](const std::string& language) {
		        if (ui::UISystem::exists()) {
			        ui::UISystem::get().setLanguage(language);
		        }
	        }
	    )
	    .addFunction(
	        "language", +[]() -> std::string { return ui::UISystem::exists() ? ui::UISystem::get().language() : std::string(); }
	    )
	    .endNamespace()

	    .beginNamespace("Physics")
	    .addFunction(
	        "shootVoxel",
	        +[](const glm::vec3& origin, const glm::vec3& direction, float max_distance, float energy, float min_radius) -> bool {
		        return toast::Engine::get() && toast::Engine::get()->shootVoxel(origin, direction, max_distance, energy, min_radius);
	        }
	    )
	    .endNamespace()

	    // DEBUG SHI
	    .beginNamespace("Debug")
	    .addFunction(
	        "drawLine",
	        +[](const glm::vec3& start, const glm::vec3& end, const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f}) {
		        debug::drawLine(start, end, color);
	        }
	    )
	    .addFunction(
	        "drawBox",
	        +[](const glm::vec3 min, const glm::vec3 max, const glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) {
		        debug::drawBox(min, max, color);
	        }
	    )
	    .addFunction(
	        "drawSphere",
	        +[](const glm::vec3& center, float radius, const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f}, int segments = 8) {
		        debug::drawSphere(center, radius, color, segments);
	        }
	    )
	    .addFunction(
	        "drawBillboard",
	        +[](glm::vec3 world_position,
					    float size,
					    assets::Handle<assets::Texture>
					        texture,
					    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}) { debug::drawBillboard(world_position, size, std::move(texture), tint); }
	    )
	    .addFunction(
	        "drawMesh",
	        +[](const assets::Handle<assets::Mesh>& mesh, const glm::mat4& transform, glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}) {
		        debug::drawMesh(mesh, transform, tint);
	        }
	    )
	    .addFunction(
	        "drawBillboard",
	        +[](glm::vec3 world_position, float size, toast::UID texture, glm::vec4 tint = glm::vec4 {1.0f, 1.0f, 1.0f, 1.0f}) {
		        debug::drawBillboard(world_position, size, texture, tint);
	        }
	    )
	    .addFunction(
	        "drawArrow",
	        +[](glm::vec3 from, glm::vec3 to, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, float head_size = 0.2f) {
		        debug::drawArrow(from, to, color, head_size);
	        }
	    )
	    .addFunction(
	        "drawCone",
	        +[](glm::vec3 apex,
					    glm::vec3 direction,
					    float length,
					    float half_angle_degrees,
					    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
					    int segments = 24) { debug::drawCone(apex, direction, length, half_angle_degrees, color, segments); }
	    )
	    .addFunction(
	        "drawFrustum",
	        +[](const toast::Camera& camera, float aspect, glm::vec4 color = {1.0f, 1.0f, 0.0f, 1.0f}, float far_override = 0.0f) {
		        debug::drawFrustum(camera, aspect, color, far_override);
	        }
	    )
	    .addFunction(
	        "drawSolidSphere",
	        +[](glm::vec3 center, float radius, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}) {
		        debug::drawSolidSphere(center, radius, color);
	        }
	    )
	    .addFunction(
	        "drawShapeBox",
	        +[](const glm::mat4& transform, glm::vec4 color, bool fill) { debug::drawShapeBox(transform, color, fill); }
	    )
	    .addFunction(
	        "drawCapsule",
	        +[](const glm::mat4& transform, float radius, float height, glm::vec4 color, bool fill) {
		        debug::drawCapsule(transform, radius, height, color, fill);
	        }
	    )
	    .endNamespace();
	registerTypeMarkers(state);
}

void LuaState::registerTypeMarkers(lua_State* state) noexcept {
	LuaEventRegistry::installDescriptors(state);
	// Node type markers
	toast::NodeRegistry::forEachType([&](const toast::NodeInfo* info) {
		const std::string_view bare = stripNamespace(info->type);
		const std::string global_name(bare);
		if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::node, std::string(info->type)}); r) {
			lua_setglobal(state, global_name.c_str());
		}
	});
	if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::node, ""}); r) {
		lua_setglobal(state, "Node");
	}

	// Asset type markers
	for (const auto& [type_str, lua_name] : assets::AssetRegistry::registeredLuaNames()) {
		if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::asset, type_str}); r) {
			lua_setglobal(state, lua_name.c_str());
		}
	}
	if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::asset, ""}); r) {
		lua_setglobal(state, "Asset");
	}

	const auto schema_entries = assets::namedSchemaEntries();
	if (!schema_entries.empty()) {
		lua_newtable(state);
		for (const auto& entry : schema_entries) {
			if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::asset, "data"}); r) {
				lua_setfield(state, -2, entry.name.c_str());
			}
		}
		lua_setglobal(state, "Schemas");
	}
}

void LuaState::refreshTypeMarkers() noexcept {
	for (size_t i = 0; i < m_pool_size; ++i) {
		Lock guard = lock(i);
		if (guard) {
			registerTypeMarkers(guard.state());
		}
	}
	TOAST_INFO("Lua", "Refreshed type markers on {} states", m_pool_size);
}

}
