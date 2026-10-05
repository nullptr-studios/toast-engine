#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <cassert>
#include <string>

using namespace toast::tests::scripting_tests;

// The callbacks a script registers on a StateMachine are called by C++ long after the script call that registered
// them returned. They must run on the interpreter that owns them, and stop running when the script that wrote them
// is rebuilt
TOAST_TEST_NAMED("Scripting", "scripting/08_state_machine_reload", test_scripting_08_state_machine_reload) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto host = toast::_detail::WorldTestAccess::createNode(*world_owner, "host");
	auto machine = toast::_detail::WorldTestAccess::createTypedNode(*world_owner, "toast::StateMachine", "machine");
	assert(machine.exists());

	auto script = makeScript(R"lua(
local M = {}
M.entered = 0
M.ticks = 0
function M:setup(machine)
    machine:addState("idle", {
        entry = function() M.entered = M.entered + 1 end,
        tick = function() M.ticks = M.ticks + 1 end,
    })
    machine:setState("idle")
end
return M
)lua");
	toast::_detail::WorldTestAccess::attachScript(*host, script);

	host->call("setup", toast::Box<toast::Node>(machine));
	toast::_detail::WorldTestAccess::callTick(*machine, toast::TickFunctionList::begin);
	toast::_detail::WorldTestAccess::callTick(*machine, toast::TickFunctionList::tick);
	assert(std::any_cast<int>(host->scriptRuntime()->getVar("entered")) == 1);
	assert(std::any_cast<int>(host->scriptRuntime()->getVar("ticks")) == 1);

	// The script is rebuilt, and the value of its exported variable carries over. The old closures belong to a runtime
	// that no longer exists, so they are skipped: the counter must not move
	host->reloadScripts();
	assert(std::any_cast<int>(host->scriptRuntime()->getVar("ticks")) == 1);
	toast::_detail::WorldTestAccess::callTick(*machine, toast::TickFunctionList::tick);
	assert(std::any_cast<int>(host->scriptRuntime()->getVar("ticks")) == 1);

	// Running setup again replaces the dead state with a live one
	host->call("setup", toast::Box<toast::Node>(machine));
	toast::_detail::WorldTestAccess::callTick(*machine, toast::TickFunctionList::tick);
	assert(std::any_cast<int>(host->scriptRuntime()->getVar("ticks")) == 2);

	// Removing the scripts altogether leaves the machine with callbacks that must be inert
	host->setScripts({});
	toast::_detail::WorldTestAccess::callTick(*machine, toast::TickFunctionList::tick);
	toast::_detail::WorldTestAccess::callTick(*machine, toast::TickFunctionList::end);
}

// A closure that captured self outlives its script when Lua keeps it (here in a global). Using it afterwards has to
// fail with a Lua error instead of reading the freed script instance
TOAST_TEST_NAMED("Scripting", "scripting/08b_stale_self_closure", test_scripting_08b_stale_self_closure) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto host = toast::_detail::WorldTestAccess::createNode(*world_owner, "host");
	toast::_detail::WorldTestAccess::attachScript(*host, makeScript(R"lua(
local M = {}
M.value = 5
function M:stash()
    -- self:name() is not a field of the table, so it goes through the metatable that points at the instance
    kept = function() return self:name() end
end
return M
)lua"));

	host->call("stash");
	const size_t vm = host->scriptRuntime()->stateIndex();
	host->reloadScripts();    // the instance that captured self is destroyed here

	std::string error;
	assert(!::scripting::LuaState::get().runStringOn(vm, "return kept()", &error));
	assert(error.find("unloaded") != std::string::npos);
}
