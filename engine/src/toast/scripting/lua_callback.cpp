#include "lua_callback.hpp"

#include "lua_state.hpp"
#include "lua_util.hpp"
#include "script_context.hpp"
#include "script_dispatch.hpp"

#include <toast/log.hpp>
#include <toast/world/node.hpp>
#include <utility>

namespace scripting {

struct LuaCallback::Shared {
	size_t vm_index = 0;
	std::unique_ptr<luabridge::LuaRef> ref;    ///< bound to the main thread of the interpreter
	std::shared_ptr<RuntimeToken> token;
	toast::Box<toast::Node> node;
	std::string name;

	Shared() = default;
	Shared(const Shared&) = delete;
	auto operator=(const Shared&) -> Shared& = delete;

	~Shared() {
		if (ref) {
			// May be the last copy dying on any thread
			// retire or leak is crazy ngl
			LuaState::retireOrLeak(vm_index, [moved = std::move(ref)]() mutable { moved.reset(); });
		}
	}

	[[nodiscard]]
	auto alive() const noexcept -> bool {
		return ref != nullptr && (token == nullptr || token->alive.load(std::memory_order_acquire));
	}
};

LuaCallback::LuaCallback(std::shared_ptr<Shared> shared) noexcept : m_shared(std::move(shared)) { }

auto LuaCallback::capture(const luabridge::LuaRef& fn, std::string name) -> LuaCallback {
	if (!fn.isFunction()) {
		return {};
	}
	lua_State* source = fn.state();
	const std::optional<size_t> index = LuaState::indexOf(source);
	if (!index.has_value() || !LuaState::exists()) {
		TOAST_WARN("Lua", "LuaCallback: '{}' does not belong to a pooled interpreter; ignoring", name);
		return {};
	}
	TOAST_LUA_ASSERT_OWNED(source);

	auto shared = std::make_shared<Shared>();
	shared->vm_index = *index;
	shared->ref = std::make_unique<luabridge::LuaRef>(fn);
	// A coroutine can be collected while the callback lives on, the main thread cannot
	lua_State* main_thread = LuaState::get().mainState(*index);
	if (shared->ref->state() != main_thread) {
		lua_checkstack(main_thread, 2);
		shared->ref->moveTo(main_thread);
	}
	const ScriptContext& context = currentScriptContext();
	shared->token = context.token;
	shared->node = context.node;
	shared->name = std::move(name);
	return LuaCallback(std::move(shared));
}

auto LuaCallback::alive() const noexcept -> bool {
	return m_shared != nullptr && m_shared->alive();
}

auto LuaCallback::invoke() const noexcept -> bool {
	std::optional<bool> unused;
	return execute(nullptr, false, unused, true);
}

auto LuaCallback::invokeBool() const noexcept -> std::optional<bool> {
	std::optional<bool> result;
	if (!execute(nullptr, true, result, false)) {
		return std::nullopt;
	}
	if (!result.has_value()) {
		TOAST_WARN("Lua", "{}: expected a boolean result", m_shared->name.empty() ? "callback" : m_shared->name);
	}
	return result;
}

auto LuaCallback::invokeWithArgument(const std::function<void(lua_State*)>& push_argument) const noexcept -> std::optional<bool> {
	std::optional<bool> result;
	if (!execute(&push_argument, true, result, false)) {
		return std::nullopt;
	}
	return result.value_or(false);
}

auto LuaCallback::execute(
    const std::function<void(lua_State*)>* push_argument, bool want_result, std::optional<bool>& result, bool allow_queue
) const noexcept -> bool {
	// Keeps the reference alive even if the last owner of this callback lets go while it runs
	const std::shared_ptr<Shared> shared = m_shared;
	if (shared == nullptr || !shared->alive() || !LuaState::exists()) {
		return false;
	}

	// A call that needs no result and no argument can wait, so it also queues behind the calls already waiting for the interpreter
	const bool can_queue = allow_queue && !want_result && push_argument == nullptr;
	LuaState::Lock guard;
	if (!can_queue || !ScriptDispatch::mustQueue(shared->vm_index)) {
		guard = LuaState::get().lock(shared->vm_index);
	}
	if (!guard) {
		// Another interpreter is owned by this thread and this one is busy or has calls waiting
		if (can_queue) {
			ScriptDispatch::enqueue(shared->vm_index, [shared] { (void)LuaCallback(shared).invoke(); });
			return true;
		}
		TOAST_WARN("Lua", "{}: its interpreter is busy; the call was skipped", shared->name.empty() ? "callback" : shared->name);
		return false;
	}
	if (!shared->alive()) {
		return false;    // the runtime was torn down while this thread waited for the interpreter
	}

	const ScriptDepthGuard depth;
	if (!depth.allowed()) {
		TOAST_ERROR(
		    "Lua", "{}: script calls are nested too deep, possible call loop", shared->name.empty() ? "callback" : shared->name
		);
		return false;
	}

	lua_State* l = guard.state();
	const ScriptNodeContextScope context(shared->node, shared->token);
	shared->ref->push(l);
	int argument_count = 0;
	if (push_argument != nullptr) {
		(*push_argument)(l);
		argument_count = 1;
	}
	if (pcallTraceback(l, argument_count, want_result ? 1 : 0) != LUA_OK) {
		TOAST_ERROR("Lua", "Error in {}: {}", shared->name.empty() ? "callback" : shared->name, lua_tostring(l, -1));
		lua_pop(l, 1);
		return false;
	}
	if (want_result) {
		result = lua_isboolean(l, -1) ? std::optional<bool> {lua_toboolean(l, -1) != 0} : std::nullopt;
		lua_pop(l, 1);
	}
	return true;
}

}
