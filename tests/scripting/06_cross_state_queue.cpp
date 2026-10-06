#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

using namespace toast::tests::scripting_tests;

// A script calling into a node whose interpreter is busy on another thread must neither wait for it (it used to stall
// for 500ms and then drop the call) nor lose the call: it is queued and delivered once nothing is executing
TOAST_TEST_NAMED("Scripting", "scripting/06_cross_state_queue", test_scripting_06_cross_state_queue) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto caller = toast::_detail::WorldTestAccess::createNode(*world_owner, "caller");
	auto target = toast::_detail::WorldTestAccess::createNode(*world_owner, "target");

	toast::_detail::WorldTestAccess::attachScript(*caller, makeScript(R"lua(
local M = {}
M.other = false
function M:callOther() self.other:call("ping", 7) end
return M
)lua"));
	toast::_detail::WorldTestAccess::attachScript(*target, makeScript(R"lua(
local M = {}
M.pings = 0
M.last = 0
function M:ping(value)
    M.pings = M.pings + 1
    M.last = value
end
return M
)lua", 2));

	const size_t target_vm = target->scriptRuntime()->stateIndex();
	assert(caller->scriptRuntime()->stateIndex() != target_vm);
	caller->scriptRuntime()->setVar("other", std::any {toast::Box<toast::Node>(target)});

	assert(::scripting::ScriptDispatch::pending() == 0);
	{
		InterpreterHolder busy(target_vm);

		const auto start = std::chrono::steady_clock::now();
		caller->call("callOther");
		const auto elapsed = std::chrono::steady_clock::now() - start;

		assert(elapsed < std::chrono::milliseconds(300));    // it did not wait for the busy interpreter
		assert(::scripting::ScriptDispatch::pending() == 1);    // and it did not drop the call
		assert(::scripting::LuaState::get().contentionCount() >= 1);
	}

	// Nothing owns an interpreter any more, so the queued call can be delivered
	[[maybe_unused]] const size_t delivered = ::scripting::ScriptDispatch::deliver();    // outside the assert, which release builds drop
	assert(delivered == 1);
	assert(::scripting::ScriptDispatch::pending() == 0);
	assert(std::any_cast<int>(target->scriptRuntime()->getVar("pings")) == 1);
	assert(std::any_cast<int>(target->scriptRuntime()->getVar("last")) == 7);
}

// Destroying a runtime while its interpreter is busy on another thread must not touch the interpreter from here:
// the release of its Lua references waits for the interpreter's next owner
TOAST_TEST_NAMED("Scripting", "scripting/06b_retire_on_busy_state", test_scripting_06b_retire_on_busy_state) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto node = toast::_detail::WorldTestAccess::createNode(*world_owner, "doomed");
	toast::_detail::WorldTestAccess::attachScript(*node, makeScript(R"lua(
local M = {}
M.value = 3
M.items = { 1, 2, 3 }
function M:tick() end
return M
)lua"));

	const size_t vm = node->scriptRuntime()->stateIndex();
	{
		InterpreterHolder busy(vm);
		node->setScripts({});    // tears the runtime down while another thread owns its interpreter
		assert(node->scriptRuntime() == nullptr);
	}

	// The queued release ran when the holder let go; the interpreter is healthy and reusable
	auto replacement = toast::_detail::WorldTestAccess::createNode(*world_owner, "replacement");
	toast::_detail::WorldTestAccess::attachScript(*replacement, makeScript("local M = {} M.value = 9 return M", 3));
	assert(std::any_cast<int>(replacement->scriptRuntime()->getVar("value")) == 9);
	auto guard = ::scripting::LuaState::get().lock(vm);
	assert(guard);
}
