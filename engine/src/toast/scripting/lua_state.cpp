#include "lua_state.hpp"

#include <cmath>
#include <format>
#include <glm/common.hpp>       // min, max, abs, clamp, mix
#include <glm/geometric.hpp>    // dot, cross, length, normalize, distance, reflect
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <toast/log.hpp>

namespace scripting {

namespace {
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
}

std::unique_ptr<LuaState> LuaState::create() noexcept {
	TOAST_ASSERT(instance == nullptr, "Lua", "LuaState instance already exists");

	class Helper : public LuaState { };

	return std::make_unique<Helper>();
}

LuaState& LuaState::get() noexcept {
	TOAST_ASSERT(instance != nullptr, "Lua", "LuaState does not exist");
	return *LuaState::instance;
}

bool LuaState::runString(std::string_view lua_code) noexcept {
	int load_status = luaL_loadbufferx(m_state, lua_code.data(), lua_code.size(), "=runString", nullptr);
	if (load_status != LUA_OK) {
		TOAST_ERROR("Lua", "Failed to load Lua code: {}", lua_tostring(m_state, -1));
		lua_pop(m_state, 1);
		return false;
	}

	int pcall_status = lua_pcall(m_state, 0, LUA_MULTRET, 0);
	if (pcall_status != LUA_OK) {
		TOAST_ERROR("Lua", "Failed to execute Lua code: {}", lua_tostring(m_state, -1));
		lua_pop(m_state, 1);
		return false;
	}

	return true;
}

LuaState::LuaState() {
	LuaState::instance = this;
	m_state = luaL_newstate();
	TOAST_ASSERT(m_state != nullptr, "Lua", "Failed to create Lua state");
	luaL_openlibs(m_state);

	lua_atpanic(m_state, [](auto* state) -> int {
		TOAST_ERROR("Lua", "Panic: {}", lua_tostring(state, -1));
		return 0;
	});

	luabridge::getGlobalNamespace(m_state).addFunction("print", luaPrint).addFunction("warn", luaWarn);

	registerApi();

	TOAST_INFO("Lua", "Created lua state");
}

LuaState::~LuaState() {
	lua_close(m_state);
	LuaState::instance = nullptr;
	TOAST_INFO("Lua", "Destroyed lua state");
}

void LuaState::registerApi() noexcept {
	using namespace luabridge;

	getGlobalNamespace(m_state)
	    .beginNamespace("toast")
	    .addFunction("trace", luaToastTrace)
	    .addFunction("info", luaToastInfo)
	    .addFunction("warn", luaToastWarn)
	    .addFunction("error", luaToastError)
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
	    .endClass();

	// color3/color4 are aliases for vec3/vec4
	luaL_dostring(m_state, "color3 = vec3\ncolor4 = vec4");
}

}
