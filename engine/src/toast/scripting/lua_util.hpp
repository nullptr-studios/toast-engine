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

/**
 * @brief Counts nested script entries on the current thread
 *
 * Signals, calls and events can bounce between nodes, and every hop is a Lua call nested inside C++ frames
 */
class ScriptDepthGuard {
public:
	ScriptDepthGuard() noexcept;
	~ScriptDepthGuard();

	ScriptDepthGuard(const ScriptDepthGuard&) = delete;
	auto operator=(const ScriptDepthGuard&) -> ScriptDepthGuard& = delete;
	ScriptDepthGuard(ScriptDepthGuard&&) = delete;
	auto operator=(ScriptDepthGuard&&) -> ScriptDepthGuard& = delete;

	/// False when the limit was reached; the caller must not enter Lua
	[[nodiscard]]
	auto allowed() const noexcept -> bool {
		return m_allowed;
	}

private:
	bool m_allowed;
};

}
