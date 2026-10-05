#include "lua_util.hpp"

#include <lua.hpp>

namespace scripting {

namespace {

constexpr int k_max_script_depth = 48;
thread_local int t_script_depth = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

auto tracebackHandler(lua_State* state) -> int {
	const char* msg = lua_tostring(state, 1);
	luaL_traceback(state, state, msg != nullptr ? msg : "(error object is not a string)", 1);
	return 1;
}

}

auto pcallTraceback(lua_State* state, int nargs, int nresults) noexcept -> int {
	const int handler_index = lua_gettop(state) - nargs;
	lua_pushcfunction(state, tracebackHandler);
	lua_insert(state, handler_index);
	const int status = lua_pcall(state, nargs, nresults, handler_index);
	lua_remove(state, handler_index);
	return status;
}

ScriptDepthGuard::ScriptDepthGuard() noexcept : m_allowed(t_script_depth < k_max_script_depth) {
	if (m_allowed) {
		++t_script_depth;
	}
}

ScriptDepthGuard::~ScriptDepthGuard() {
	if (m_allowed) {
		--t_script_depth;
	}
}

}
