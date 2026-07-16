#include "lua_util.hpp"

#include <lua.hpp>

namespace scripting {

namespace {

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

}
