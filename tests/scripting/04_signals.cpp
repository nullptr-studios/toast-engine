#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <cassert>
#include <string>

using namespace toast::tests::scripting_tests;

TOAST_TEST_NAMED("Scripting", "scripting/04_signals", test_scripting_04_signals) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto node = toast::_detail::WorldTestAccess::createNode(*world_owner, "host");

	toast::_detail::WorldTestAccess::attachScript(*node, makeScript(R"lua(
local M = {}
M.value = 0
M.lua_signal = Signal:create()

function M:setup()
    -- forwards_args is intentionally omitted: it defaults to true.
    M.connected = self.on_enable:connect(self, "onEnabled") and self.on_disable:connect(self, "onDisabled")
end

function M:onEnabled(source)
    M.enabled_source = source:name()
end

function M:onDisabled(source)
    M.disabled_source = source:name()
end

function M:disconnectSignal()
    M.disconnected = self.on_enable:disconnect(self, "onEnabled") and self.on_disable:disconnect(self, "onDisabled")
end

function M:setupLuaSignal()
    M.lua_connected = self.lua_signal:connect(self, "onLuaSignal")
end

function M:onLuaSignal()
    M.lua_fired = true
end

function M:fireLuaSignal()
    self.lua_signal:fire()
end

return M
)lua"));

	node->enabled(false);
	node->call("setup");
	assert(std::any_cast<bool>(node->scriptRuntime()->getVar("connected")));
	assert(node->on_enable.connections().size() == 1);
	assert(node->on_enable.connections().front().source == signals::ConnectionSource::lua);

	node->enabled(true);
	assert(std::any_cast<std::string>(node->scriptRuntime()->getVar("enabled_source")) == "host");

	node->enabled(false);
	assert(std::any_cast<std::string>(node->scriptRuntime()->getVar("disabled_source")) == "host");

	node->call("disconnectSignal");
	assert(std::any_cast<bool>(node->scriptRuntime()->getVar("disconnected")));
	assert(node->on_enable.connections().empty());
	assert(node->on_disable.connections().empty());

	node->call("setupLuaSignal");
	assert(std::any_cast<bool>(node->scriptRuntime()->getVar("lua_connected")));
	node->call("fireLuaSignal");
	assert(std::any_cast<bool>(node->scriptRuntime()->getVar("lua_fired")));
}
