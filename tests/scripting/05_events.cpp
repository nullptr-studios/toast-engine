#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/events/event.hpp"
#include "toast/events/listener.hpp"
#include "toast/physics/contact_events.hpp"
#include "toast/scripting/script_runtime.hpp"
#include "toast/window/window_events.hpp"
#include "toast/world/workspace_events.hpp"

#include <any>
#include <cassert>

using namespace toast::tests::scripting_tests;

TOAST_TEST_NAMED("Scripting", "scripting/05_events", test_scripting_05_events) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto node = toast::_detail::WorldTestAccess::createNode(*world_owner, "event host");
	auto script = makeScript(R"lua(
local M = {}
M.named = 0
M.inline = 0
M.consume = false

function M:setup()
    self.listener:subscribe(Events.WindowResize, "onResize")
    self.listener:subscribe(Events.WindowResize, function(e)
        M.inline = M.inline + e.height
        if e.width == 1 then event.send(Events.WindowResize, { width = 2, height = 3 }) end
    end)
end

function M:onResize(e)
    M.named = M.named + e.width
    return M.consume
end

function M:sendResize()
    event.send(Events.WindowResize, { width = 4, height = 5 })
end

function M:sendInvalid()
    event.send(Events.WindowResize, { width = "bad", height = 5 })
end

function M:consumeNext() M.consume = true end
function M:unsubscribeNamed() self.listener:unsubscribe(Events.WindowResize, "onResize") end
function M:unsubscribeAll() self.listener:unsubscribe(Events.WindowResize) end
return M
)lua");
	toast::_detail::WorldTestAccess::attachScript(*node, script);
	node->call("setup");

	event::Listener observer;
	int observed = 0;
	observer.subscribe<event::WindowResize>([&](const event::WindowResize&) { ++observed; });

	node->call("sendResize");
	event::pollEvents();
	assert(std::any_cast<int>(node->scriptRuntime()->getVar("named")) == 4);
	assert(std::any_cast<int>(node->scriptRuntime()->getVar("inline")) == 5);
	assert(observed == 1);
	node->call("sendInvalid");
	event::pollEvents();
	assert(observed == 1);    // validation rejected the malformed payload
	node->call("consumeNext");
	event::send<event::WindowResize>(8, 9);
	event::pollEvents();
	assert(std::any_cast<int>(node->scriptRuntime()->getVar("named")) == 12);
	assert(std::any_cast<int>(node->scriptRuntime()->getVar("inline")) == 5);
	assert(observed == 1);

	node->call("unsubscribeNamed");
	event::send<event::WindowResize>(1, 2);
	event::pollEvents();
	assert(std::any_cast<int>(node->scriptRuntime()->getVar("inline")) == 7);
	assert(observed == 2);
	event::pollEvents();    // nested width=2 event queued by the inline callback
	assert(std::any_cast<int>(node->scriptRuntime()->getVar("inline")) == 10);
	assert(observed == 3);

	node->call("unsubscribeAll");
	event::send<event::WindowResize>(10, 10);
	event::pollEvents();
	assert(std::any_cast<int>(node->scriptRuntime()->getVar("inline")) == 10);
	assert(observed == 4);

	// Rebuilding the runtime must remove subscriptions owned by the old runtime.
	node->call("setup");
	script->setData([] {
		constexpr std::string_view replacement = "local M = {}\nM.reloaded = true\nreturn M\n";
		return std::vector<uint8_t>(replacement.begin(), replacement.end());
	}());
	node->reloadScripts();
	event::send<event::WindowResize>(20, 20);
	event::pollEvents();
	assert(std::any_cast<bool>(node->scriptRuntime()->getVar("reloaded")));
	assert(observed == 5);

	// A subscription on another pooled interpreter receives an independently marshalled table.
	auto other = toast::_detail::WorldTestAccess::createNode(*world_owner, "other event host");
	auto other_script = makeScript(
	    R"lua(
local M = { total = 0 }
function M:setup()
    self.listener:subscribe(Events.WindowResize, function(e) M.total = M.total + e.width + e.height end)
end
return M
)lua",
	    2
	);
	toast::_detail::WorldTestAccess::attachScript(*other, other_script);
	other->call("setup");
	assert(other->scriptRuntime()->stateIndex() != node->scriptRuntime()->stateIndex());
	event::send<event::WindowResize>(3, 4);
	event::pollEvents();
	assert(std::any_cast<int>(other->scriptRuntime()->getVar("total")) == 7);
	assert(observed == 6);
	other_script->setData([] {
		constexpr std::string_view replacement = "local M = {}\nreturn M\n";
		return std::vector<uint8_t>(replacement.begin(), replacement.end());
	}());
	other->reloadScripts();

	// I hate  having to modify tests lol this is not fun without a clanker
	auto typed = toast::_detail::WorldTestAccess::createNode(*world_owner, "typed event host");
	auto typed_script = makeScript(
	    R"lua(
local M = { bytes = 0, files = 0, first_target_ok = false, voxel_sum = 0, contacts = -1, receive_only_rejected = false }
function M:setup()
    self.listener:subscribe(Events.WorkspaceSaveCompleted, function(e)
        for _, b in ipairs(e.snapshot) do M.bytes = M.bytes + b end
    end)
    self.listener:subscribe(Events.WindowDrop, function(e) M.files = #e.files end)
    self.listener:subscribe(Events.VoxelCollapsePieces, function(e)
        M.first_target_ok = type(e.targets[1]) == "string" and #e.targets[1] == 11
        event.send(Events.VoxelCollapsePieces, { targets = { e.targets[2] }, path = "echo" })
    end)
    self.listener:subscribe(Events.VoxelBucketFill, function(e) M.voxel_sum = math.tointeger(e.voxel.x + e.voxel.y + e.voxel.z) end)
    self.listener:subscribe(Events.ContactBegin, function(e) M.contacts = #e.contact.contacts end)
end
function M:sendTyped()
    event.send(Events.WorkspaceSaveCompleted, { workspace_handle = 1, request = 2, success = true, snapshot = { 1, 2, 3 }, error = "" })
    event.send(Events.VoxelBucketFill, { target = "", voxel = vec3(1.2, 2, 3), id = 0, search = false, search_axis = 0, search_step = 1 })
    M.receive_only_rejected = not pcall(event.send, Events.ContactBegin, { contact = {} })
end
return M
)lua",
	    3
	);
	toast::_detail::WorldTestAccess::attachScript(*typed, typed_script);
	typed->call("setup");

	event::Listener typed_observer;
	std::vector<uint8_t> observed_snapshot;
	glm::ivec3 observed_voxel {};
	std::vector<toast::UID> echoed;
	typed_observer.subscribe<event::WorkspaceSaveCompleted>([&](const event::WorkspaceSaveCompleted& e) {
		observed_snapshot = e.snapshot;
	});
	typed_observer.subscribe<event::VoxelBucketFill>([&](const event::VoxelBucketFill& e) { observed_voxel = e.voxel; });
	typed_observer.subscribe<event::VoxelCollapsePieces>([&](const event::VoxelCollapsePieces& e) {
		if (e.path == "echo") {
			echoed = e.targets;
		}
	});

	typed->call("sendTyped");
	event::send<event::WindowDrop>(std::vector<std::string> {"a.png", "b.png"});
	event::VoxelCollapsePieces pieces;
	pieces.targets = {toast::UID(5), toast::UID(6)};
	event::send<event::VoxelCollapsePieces>(std::move(pieces));
	event::send<event::ContactBegin>(physics::Manifold {.contact_count = 2});
	event::pollEvents();
	event::pollEvents();    // echo queued by the VoxelCollapsePieces callback
	auto typed_var = [&](std::string_view name) { return typed->scriptRuntime()->getVar(name); };
	assert(std::any_cast<int>(typed_var("bytes")) == 6);
	assert((observed_snapshot == std::vector<uint8_t> {1, 2, 3}));
	assert(observed_voxel == glm::ivec3(1, 2, 3));
	assert(std::any_cast<int>(typed_var("voxel_sum")) == 6);
	assert(std::any_cast<int>(typed_var("files")) == 2);
	assert(std::any_cast<bool>(typed_var("first_target_ok")));
	assert(echoed == std::vector<toast::UID> {toast::UID(6)});
	assert(std::any_cast<int>(typed_var("contacts")) == 2);
	assert(std::any_cast<bool>(typed_var("receive_only_rejected")));
	typed_script->setData([] {
		constexpr std::string_view replacement = "local M = {}\nreturn M\n";
		return std::vector<uint8_t>(replacement.begin(), replacement.end());
	}());
	typed->reloadScripts();
}
