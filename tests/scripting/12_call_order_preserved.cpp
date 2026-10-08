#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <cassert>

using namespace toast::tests::scripting_tests;

// The calls into one interpreter are applied in the order they were issued, whether they ran at once or had to wait.
// Without that, a call that finds the interpreter free overtakes the call before it that was queued while it was busy.
// Each call appends its digit to seq, so the order shows in the number: 12 is in order, 21 is overtaken
TOAST_TEST_NAMED("Scripting", "scripting/12_call_order_preserved", test_scripting_12_call_order_preserved) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto caller = toast::_detail::WorldTestAccess::createNode(*world_owner, "caller");
	auto target = toast::_detail::WorldTestAccess::createNode(*world_owner, "target");

	toast::_detail::WorldTestAccess::attachScript(*caller, makeScript(R"lua(
local M = {}
M.other = false
function M:sendFirst() self.other:call("record", 1) end
function M:sendSecond() self.other:call("record", 2) end
return M
)lua"));
	toast::_detail::WorldTestAccess::attachScript(*target, makeScript(R"lua(
local M = {}
M.seq = 0
function M:record(value) M.seq = M.seq * 10 + value end
return M
)lua", 2));

	const size_t target_vm = target->scriptRuntime()->stateIndex();
	assert(caller->scriptRuntime()->stateIndex() != target_vm);
	caller->scriptRuntime()->setVar("other", std::any {toast::Box<toast::Node>(target)});

	assert(::scripting::ScriptDispatch::pending() == 0);
	{
		InterpreterHolder busy(target_vm);
		caller->call("sendFirst");
		assert(::scripting::ScriptDispatch::pending() == 1);    // the target is busy, so the first call waits
	}

	// The target is free now, but the first call is still waiting, so the second one has to wait behind it
	caller->call("sendSecond");
	assert(::scripting::ScriptDispatch::pending() == 2);
	assert(std::any_cast<int>(target->scriptRuntime()->getVar("seq")) == 0);

	[[maybe_unused]] const size_t delivered = ::scripting::ScriptDispatch::deliver();    // outside the assert, which release builds drop
	assert(delivered == 2);
	assert(std::any_cast<int>(target->scriptRuntime()->getVar("seq")) == 12);
}

// Calls issued while a batch is being delivered queue behind the calls of that batch that have not run yet. Here the
// relay's forward() is delivered first and calls the target while record(1), issued before it ran, is still waiting
TOAST_TEST_NAMED("Scripting", "scripting/12b_call_order_during_delivery", test_scripting_12b_call_order_during_delivery) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto source = toast::_detail::WorldTestAccess::createNode(*world_owner, "source");
	auto relay = toast::_detail::WorldTestAccess::createNode(*world_owner, "relay");
	auto target = toast::_detail::WorldTestAccess::createNode(*world_owner, "target");

	toast::_detail::WorldTestAccess::attachScript(*source, makeScript(R"lua(
local M = {}
M.relay = false
M.target = false
function M:callRelay() self.relay:call("forward") end
function M:callTarget() self.target:call("record", 1) end
return M
)lua"));
	toast::_detail::WorldTestAccess::attachScript(*relay, makeScript(R"lua(
local M = {}
M.target = false
function M:forward() self.target:call("record", 2) end
return M
)lua", 2));
	toast::_detail::WorldTestAccess::attachScript(*target, makeScript(R"lua(
local M = {}
M.seq = 0
function M:record(value) M.seq = M.seq * 10 + value end
return M
)lua", 3));

	const size_t relay_vm = relay->scriptRuntime()->stateIndex();
	const size_t target_vm = target->scriptRuntime()->stateIndex();
	const size_t source_vm = source->scriptRuntime()->stateIndex();
	assert(source_vm != relay_vm && source_vm != target_vm && relay_vm != target_vm);
	source->scriptRuntime()->setVar("relay", std::any {toast::Box<toast::Node>(relay)});
	source->scriptRuntime()->setVar("target", std::any {toast::Box<toast::Node>(target)});
	relay->scriptRuntime()->setVar("target", std::any {toast::Box<toast::Node>(target)});

	assert(::scripting::ScriptDispatch::pending() == 0);
	{
		InterpreterHolder busy_relay(relay_vm);
		InterpreterHolder busy_target(target_vm);
		source->call("callRelay");     // queued first
		source->call("callTarget");    // queued second
		assert(::scripting::ScriptDispatch::pending() == 2);
	}

	// forward() and record(1) are delivered, and the record(2) that forward() issues has to wait for the next round
	[[maybe_unused]] const size_t delivered = ::scripting::ScriptDispatch::deliver();    // outside the assert, which release builds drop
	assert(delivered == 3);
	assert(::scripting::ScriptDispatch::pending() == 0);
	assert(std::any_cast<int>(target->scriptRuntime()->getVar("seq")) == 12);
}

// A thread that owns no interpreter waits for the one it needs and runs at once, even while calls for it are waiting.
// Nothing can overtake it, and queueing it would reorder the C++ and Lua pieces of a node's own tick
TOAST_TEST_NAMED("Scripting", "scripting/12c_free_thread_runs_at_once", test_scripting_12c_free_thread_runs_at_once) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto caller = toast::_detail::WorldTestAccess::createNode(*world_owner, "caller");
	auto target = toast::_detail::WorldTestAccess::createNode(*world_owner, "target");

	toast::_detail::WorldTestAccess::attachScript(*caller, makeScript(R"lua(
local M = {}
M.other = false
function M:sendFirst() self.other:call("record", 1) end
return M
)lua"));
	toast::_detail::WorldTestAccess::attachScript(*target, makeScript(R"lua(
local M = {}
M.seq = 0
function M:record(value) M.seq = M.seq * 10 + value end
return M
)lua", 2));

	const size_t target_vm = target->scriptRuntime()->stateIndex();
	assert(caller->scriptRuntime()->stateIndex() != target_vm);
	caller->scriptRuntime()->setVar("other", std::any {toast::Box<toast::Node>(target)});

	assert(::scripting::ScriptDispatch::pending() == 0);
	{
		InterpreterHolder busy(target_vm);
		caller->call("sendFirst");
		assert(::scripting::ScriptDispatch::pending() == 1);
	}

	// This thread owns nothing, so it takes the target itself instead of queueing behind the call that waits
	target->call("record", 3);
	assert(::scripting::ScriptDispatch::pending() == 1);
	assert(std::any_cast<int>(target->scriptRuntime()->getVar("seq")) == 3);

	[[maybe_unused]] const size_t delivered = ::scripting::ScriptDispatch::deliver();    // outside the assert, which release builds drop
	assert(delivered == 1);
	assert(std::any_cast<int>(target->scriptRuntime()->getVar("seq")) == 31);
}
