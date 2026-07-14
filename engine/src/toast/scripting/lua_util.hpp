/**
 * @file lua_util.hpp
 * @author Xein
 * @date 13 Jul 2026
 *
 * @brief Small shared helpers for entering Lua safely
 */

#pragma once

struct lua_State;

namespace scripting {

auto pcallTraceback(lua_State* state, int nargs, int nresults) noexcept -> int;

}
