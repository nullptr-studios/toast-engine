/**
 * @file lua_callback.hpp
 * @author Xein
 * @date 5 Oct 2026
 *
 * @brief The only sanctioned way for C++ to keep a Lua function
 *
 * A raw luabridge::LuaRef stored in C++ is a trap: calling it touches an interpreter the calling thread may not
 * own, copying or destroying it does the same, and it keeps pointing into a script that may have been reloaded.
 * A LuaCallback owns its reference on the interpreter's main thread, calls only while it owns the interpreter,
 * releases the reference through LuaState::retire() and checks the lifetime token of the runtime that created it
 */

#pragma once

#include <functional>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <memory>
#include <optional>
#include <string>
#include <toast/export.hpp>

namespace scripting {

class TOAST_API LuaCallback {
public:
	LuaCallback() = default;

	/**
	 * @brief Keeps `fn`, which must be a Lua function, alive on its interpreter
	 *
	 * Takes the node and the lifetime token of the script that is running on this thread, so the callback stops
	 * working when that runtime is torn down or rebuilt. Has to be called by the owner of the interpreter
	 */
	[[nodiscard]]
	static auto capture(const luabridge::LuaRef& fn, std::string name = {}) -> LuaCallback;

	[[nodiscard]]
	explicit operator bool() const noexcept {
		return m_shared != nullptr;
	}

	/// False once the runtime that created the function is gone
	[[nodiscard]]
	auto alive() const noexcept -> bool;

	/**
	 * @brief Calls the function with no arguments and ignores the result
	 *
	 * When the interpreter is busy on another thread while this one owns a different one, the call is queued
	 * in ScriptDispatch instead of waiting
	 * @return false when the callback is dead or the call failed
	 */
	auto invoke() const noexcept -> bool;

	/// Calls the function and expects a boolean back. Empty when it is dead, busy, failed or returned something else
	[[nodiscard]]
	auto invokeBool() const noexcept -> std::optional<bool>;

	/**
	 * @brief Calls the function with one argument pushed by `push_argument`
	 * @return the boolean the function returned, false for anything else. Empty when the call did not run
	 *
	 * Never queued, the argument only exists for the duration of the call
	 */
	[[nodiscard]]
	auto invokeWithArgument(const std::function<void(lua_State*)>& push_argument) const noexcept -> std::optional<bool>;

private:
	struct Shared;
	explicit LuaCallback(std::shared_ptr<Shared> shared) noexcept;

	/// Single entry point behind the public calls
	auto execute(
	    const std::function<void(lua_State*)>* push_argument, bool want_result, std::optional<bool>& result, bool allow_queue
	) const noexcept -> bool;

	std::shared_ptr<Shared> m_shared;
};

}
