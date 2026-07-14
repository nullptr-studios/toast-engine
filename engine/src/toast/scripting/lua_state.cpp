#include "lua_state.hpp"

#include "asset_proxy.hpp"
#include "lua_types.hpp"
#include "lua_util.hpp"
#include "node_proxy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <toast/assets/asset_registry.hpp>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/time.hpp>
#include <tracy/Tracy.hpp>
#include <tracy/TracyLua.hpp>

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
			output += "\t";
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
			output += "\t";
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

thread_local std::vector<size_t> t_held_states;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

constexpr auto k_cross_state_timeout = std::chrono::milliseconds(500);
}

LuaState::Lock::Lock(std::unique_lock<std::recursive_timed_mutex> lock, lua_State* state, size_t index) noexcept
    : m_lock(std::move(lock)),
      m_state(state),
      m_index(index) {
	if (m_lock.owns_lock()) {
		t_held_states.push_back(m_index);
	}
}

LuaState::Lock::Lock(Lock&& other) noexcept : m_lock(std::move(other.m_lock)), m_state(other.m_state), m_index(other.m_index) {
	other.m_state = nullptr;
}

auto LuaState::Lock::operator=(Lock&& other) noexcept -> Lock& {
	if (this != &other) {
		if (m_lock.owns_lock()) {
			t_held_states.pop_back();
		}
		m_lock = std::move(other.m_lock);
		m_state = other.m_state;
		m_index = other.m_index;
		other.m_state = nullptr;
	}
	return *this;
}

LuaState::Lock::~Lock() {
	if (m_lock.owns_lock()) {
		t_held_states.pop_back();
	}
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

auto LuaState::lock(size_t index) noexcept -> Lock {
	Entry& entry = m_entries[index];

	std::unique_lock<std::recursive_timed_mutex> guard(entry.mutex, std::try_to_lock);
	if (!guard.owns_lock()) {
		ZoneScopedN("Lua lock wait");    // NOLINT
		ZoneNameF("Lua lock wait #%d", static_cast<int>(index));

		const bool holds_other = !t_held_states.empty() && std::ranges::find(t_held_states, index) == t_held_states.end();
		if (holds_other) {
			if (!guard.try_lock_for(k_cross_state_timeout)) {
				TOAST_ERROR(
				    "Lua", "Cross-state call timed out acquiring Lua state #{}; skipping (possible call cycle between nodes)", index
				);
				return {};
			}
		} else {
			guard.lock();
		}
	}
	return {std::move(guard), entry.state, index};
}

auto LuaState::tryLock(size_t index) noexcept -> Lock {
	Entry& entry = m_entries[index];
	std::unique_lock<std::recursive_timed_mutex> guard(entry.mutex, std::try_to_lock);
	if (!guard.owns_lock()) {
		return {};
	}
	return {std::move(guard), entry.state, index};
}

void LuaState::plotMemory() noexcept {
#ifdef TRACY_ENABLE
	// Tracy keeps plot names by pointer, so they need stable storage
	static const auto plot_names = [] {
		std::array<std::string, pool_size> names;
		for (size_t i = 0; i < pool_size; ++i) {
			names[i] = std::format("Lua memory #{} (KB)", i);
		}
		return names;
	}();

	for (size_t i = 0; i < pool_size; ++i) {
		Lock guard = tryLock(i);
		if (!guard) {
			continue;    // busy running a script; sample it next time
		}
		const auto kilobytes = static_cast<int64_t>(lua_gc(guard.state(), LUA_GCCOUNT));
		TracyPlot(plot_names[i].c_str(), kilobytes);
	}
#endif
}

auto LuaState::nextIndex() noexcept -> size_t {
	return m_next_index.fetch_add(1, std::memory_order_relaxed) % pool_size;
}

auto LuaState::runString(std::string_view lua_code) noexcept -> bool {
	Lock guard = lock(0);
	if (!guard) {
		return false;
	}
	lua_State* state = guard.state();

	int load_status = luaL_loadbufferx(state, lua_code.data(), lua_code.size(), "=runString", nullptr);
	if (load_status != LUA_OK) {
		TOAST_ERROR("Lua", "Failed to load Lua code: {}", lua_tostring(state, -1));
		lua_pop(state, 1);
		return false;
	}

	int pcall_status = pcallTraceback(state, 0, LUA_MULTRET);
	if (pcall_status != LUA_OK) {
		TOAST_ERROR("Lua", "Failed to execute Lua code: {}", lua_tostring(state, -1));
		lua_pop(state, 1);
		return false;
	}

	return true;
}

LuaState::LuaState() {
	LuaState::instance = this;

	for (Entry& entry : m_entries) {
		entry.state = luaL_newstate();
		TOAST_ASSERT(entry.state != nullptr, "Lua", "Failed to create Lua state");
		luaL_openlibs(entry.state);

		lua_atpanic(entry.state, [](auto* state) -> int {
			TOAST_ERROR("Lua", "Panic: {}", lua_tostring(state, -1));
			return 0;
		});

		luabridge::getGlobalNamespace(entry.state).addFunction("print", luaPrint).addFunction("warn", luaWarn);

		// Tracy's lua-side profiling API (tracy.ZoneBegin/ZoneBeginN/ZoneEnd/Message);
		// registers no-op stubs when TRACY_ENABLE is off
		tracy::LuaRegister(entry.state);

		registerApi(entry.state);
	}

	TOAST_INFO("Lua", "Created pool of {} lua states", pool_size);
}

LuaState::~LuaState() noexcept {
	for (Entry& entry : m_entries) {
		lua_close(entry.state);
	}
	LuaState::instance = nullptr;
	TOAST_INFO("Lua", "Destroyed lua state pool");
}

void LuaState::registerApi(lua_State* state) noexcept {
	using namespace luabridge;

	getGlobalNamespace(state)
	    .beginNamespace("toast")
	    .addFunction("trace", luaToastTrace)
	    .addFunction("info", luaToastInfo)
	    .addFunction("warn", luaToastWarn)
	    .addFunction("error", luaToastError)

	    .addFunction(
	        "load", +[](const std::string& path) -> AssetProxy { return AssetProxy(assets::load(path)); }
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

	    // AssetProxy
	    .beginClass<AssetProxy>("Asset")
	    .addFunction("path", &AssetProxy::path)
	    .addFunction("uid", [](const AssetProxy& a) { return static_cast<lua_Integer>(a.uid().data()); })
	    .addFunction("hasValue", &AssetProxy::hasValue)
	    .addFunction("type", &AssetProxy::type)
	    .addFunction("__tostring", &AssetProxy::toString)
	    .endClass()

	    // NodeProxy
	    .beginClass<NodeProxy>("Node")
	    .addFunction("exists", &NodeProxy::exists)
	    .addFunction("name", &NodeProxy::name)
	    .addFunction("uid", [](const NodeProxy& np) { return static_cast<lua_Integer>(np.uid()); })
	    .addFunction("find", &NodeProxy::find)
	    .addFunction("search", &NodeProxy::search)
	    .addFunction("create", &NodeProxy::create)
	    .addFunction("addDependsOn", &NodeProxy::addDependsOn)
	    .addFunction("call", &NodeProxy::call)
	    .addIndexMetaMethod(nodeProxyIndex)
	    .addNewIndexMetaMethod(nodeProxyNewindex)
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
	    .endNamespace();

	registerTypeMarkers(state);
}

void LuaState::registerTypeMarkers(lua_State* state) noexcept {
	// Node type markers
	toast::NodeRegistry::forEachType([&](const toast::NodeInfo* info) {
		const std::string_view bare = stripNamespace(info->type);
		const std::string global_name(bare);
		if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::Node, std::string(info->type)}); r) {
			lua_setglobal(state, global_name.c_str());
		}
	});
	if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::Node, ""}); r) {
		lua_setglobal(state, "Node");
	}

	// Asset type markers
	for (const auto& [type_str, lua_name] : assets::AssetRegistry::registeredLuaNames()) {
		if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::Asset, type_str}); r) {
			lua_setglobal(state, lua_name.c_str());
		}
	}
	if (auto r = luabridge::Stack<TypeMarker>::push(state, TypeMarker {TypeMarker::Kind::Asset, ""}); r) {
		lua_setglobal(state, "Asset");
	}
}

void LuaState::refreshTypeMarkers() noexcept {
	for (size_t i = 0; i < pool_size; ++i) {
		Lock guard = lock(i);
		if (guard) {
			registerTypeMarkers(guard.state());
		}
	}
	TOAST_INFO("Lua", "Refreshed type markers on {} states", pool_size);
}

}
