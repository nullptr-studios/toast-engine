/**
 * @file lua_callback.hpp
 * @author Xein
 * @date 5 Oct 2026
 *
 * @brief The only sanctioned way for C++ to keep a Lua function
 */

#pragma once

#include <cstdint>
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
	 * @brief Keeps function alive on its interpreter
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
	 * When the interpreter is busy on another thread while this one owns a different one
	 */
	[[nodiscard]]
	auto invoke() const noexcept -> bool;

	/// Calls the function and expects a boolean back
	[[nodiscard]]
	auto invokeBool() const noexcept -> std::optional<bool>;

	/**
	 * @brief Calls the function with one argument
	 * @return the boolean the function returned
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
