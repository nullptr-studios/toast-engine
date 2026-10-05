#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"
#include "toast/scripting/script_runtime.hpp"
#include "toast/window/window_events.hpp"

#include <any>
#include <cassert>
#include <vector>

using namespace toast::tests::scripting_tests;

// Rebuilding the scripts of a node that already went through its lifecycle (the editor does it when a script file
// changes) tells the old scripts they are leaving, the way they would at the end of the node's life, and brings the new
// ones to the same point. Each hook reports itself through an event so the order can be observed from outside
TOAST_TEST_NAMED("Scripting", "scripting/11_reload_lifecycle", test_scripting_11_reload_lifecycle) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto node = toast::_detail::WorldTestAccess::createNode(*world_owner, "host");

	toast::_detail::WorldTestAccess::attachScript(*node, makeScript(R"lua(
local M = {}
local function mark(n) event.send(Events.WindowResize, { width = n, height = 0 }) end
function M:load() mark(1) end
function M:init() mark(2) end
function M:begin() mark(3) end
function M:onEnable() mark(4) end
function M:onDisable() mark(5) end
function M:_end() mark(6) end
function M:destroy() mark(7) end
return M
)lua"));

	std::vector<int> seen;
	event::Listener observer;
	observer.subscribe<event::WindowResize>([&](const event::WindowResize& e) { seen.push_back(e.width); });

	using toast::TickFunctionList;
	toast::_detail::WorldTestAccess::callTick(*node, TickFunctionList::load);
	toast::_detail::WorldTestAccess::callTick(*node, TickFunctionList::init);
	toast::_detail::WorldTestAccess::callTick(*node, TickFunctionList::begin);
	toast::_detail::WorldTestAccess::callTick(*node, TickFunctionList::on_enable);
	event::pollEvents();
	assert((seen == std::vector<int> {1, 2, 3, 4}));

	// The old scripts leave in reverse, then the new ones arrive in order
	seen.clear();
	node->reloadScripts();
	event::pollEvents();
	assert((seen == std::vector<int> {5, 6, 7, 1, 2, 3, 4}));

	// A node that never got past loading is only brought that far
	auto fresh = toast::_detail::WorldTestAccess::createNode(*world_owner, "fresh");
	toast::_detail::WorldTestAccess::attachScript(*fresh, makeScript(R"lua(
local M = {}
local function mark(n) event.send(Events.WindowResize, { width = n, height = 0 }) end
function M:load() mark(1) end
function M:init() mark(2) end
function M:destroy() mark(7) end
return M
)lua", 2));
	toast::_detail::WorldTestAccess::callTick(*fresh, TickFunctionList::load);
	event::pollEvents();
	seen.clear();
	fresh->reloadScripts();
	event::pollEvents();
	assert((seen == std::vector<int> {1}));
}

// Connections made in the editor to a signal the script declares belong to the old runtime, so a rebuild has to
// make them again on the new one
TOAST_TEST_NAMED("Scripting", "scripting/11b_reload_keeps_editor_signals", test_scripting_11b_reload_keeps_editor_signals) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto source = toast::_detail::WorldTestAccess::createNode(*world_owner, "source");
	auto target = toast::_detail::WorldTestAccess::createNode(*world_owner, "target");
	toast::_detail::WorldTestAccess::attachChild(*source, *target);

	toast::_detail::WorldTestAccess::attachScript(*source, makeScript(R"lua(
local M = {}
M.fired = Signal:create()
return M
)lua"));
	toast::_detail::WorldTestAccess::attachScript(*target, makeScript(R"lua(
local M = {}
M.hits = 0
function M:onFired() M.hits = M.hits + 1 end
return M
)lua", 2));

	assert(source->scriptRuntime()->connectLuaSignal("fired", *target, "onFired", false));
	assert(source->scriptRuntime()->luaSignalConnections("fired").size() == 1);

	source->reloadScripts();

	const auto connections = source->scriptRuntime()->luaSignalConnections("fired");
	assert(connections.size() == 1);
	assert(connections.front().source == signals::ConnectionSource::editor);
	assert(connections.front().function == "onFired");
}
