#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <cassert>
#include <string>
#include <vector>

using namespace toast::tests::scripting_tests;

// Scripts that tick on different workers of one wave look around the node tree (find, getChildren) while others of the
// same wave create nodes in it. The tree is shared between those workers, so every look has to see the tree as it is at
// one moment: the node that never moves is always found and the list of children never gets shorter, and every node a
// script created is in the tree afterwards
TOAST_TEST_NAMED("Scripting", "scripting/16_scripts_read_the_tree_while_others_create", test_scripting_16_scripts_read_the_tree_while_others_create) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int pair_count = 4;
	constexpr int frame_count = 120;

	auto root = toast::_detail::WorldTestAccess::createNode(world, "root");
	auto anchor = toast::_detail::WorldTestAccess::createNode(world, "anchor");
	toast::_detail::WorldTestAccess::attachChild(*root, *anchor);

	// Each reader looks at its own children and at the anchor. Its creator adds children to it
	std::vector<toast::Box<toast::Node>> readers;
	std::vector<toast::Box<toast::Node>> creators;
	for (int i = 0; i < pair_count; ++i) {
		auto reader = toast::_detail::WorldTestAccess::createNode(world, "reader" + std::to_string(i));
		toast::_detail::WorldTestAccess::attachChild(*root, *reader);
		toast::_detail::WorldTestAccess::attachScript(*reader, makeScript(R"lua(
local M = {}
M.last = 0
M.missed = 0
M.shrunk = 0
M.looks = 0
function M:tick()
    local count = #self:getChildren()
    if count < M.last then M.shrunk = M.shrunk + 1 end
    M.last = count
    if self:find("node://world/anchor") == nil then M.missed = M.missed + 1 end
    M.looks = M.looks + 1
end
return M
)lua", 4100 + i));
		readers.push_back(reader);

		auto creator = toast::_detail::WorldTestAccess::createNode(world, "creator" + std::to_string(i));
		toast::_detail::WorldTestAccess::attachChild(*root, *creator);
		toast::_detail::WorldTestAccess::attachScript(*creator, makeScript(R"lua(
local M = {}
M.target = false
function M:tick()
    self.target:create("toast::Node")
end
return M
)lua", 4000 + i));
		creator->scriptRuntime()->setVar("target", std::any {toast::Box<toast::Node>(reader)});
		creators.push_back(creator);
	}
	toast::_detail::WorldTestAccess::setWorldRoot(world, *root);

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);
	for (int frame = 0; frame < frame_count; ++frame) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}
	while (::scripting::ScriptDispatch::pending() != 0) {
		::scripting::ScriptDispatch::deliver();
	}

	for (auto& reader : readers) {
		assert(reader->childrenSnapshot().size() == static_cast<size_t>(frame_count));
		assert(std::any_cast<int>(reader->scriptRuntime()->getVar("looks")) == frame_count);
		assert(std::any_cast<int>(reader->scriptRuntime()->getVar("missed")) == 0);
		assert(std::any_cast<int>(reader->scriptRuntime()->getVar("shrunk")) == 0);
	}
}
