#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <cassert>
#include <string>

using namespace toast::tests::scripting_tests;

// Name-based get/set resolves to the first script defining the name; path-based access
// reaches into groups; hot reload preserves values whose declaration survived.
TOAST_TEST_NAMED("Scripting", "scripting/03_vars_and_reload", test_scripting_03_vars_and_reload) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto node = toast::_detail::WorldTestAccess::createNode(*world_owner, "host");

	auto script = makeScript(R"lua(
local M = {}
M.health = 100
M.speed = 2.0
M.movement = { accel = 4.0 }
return M
)lua");
	toast::_detail::WorldTestAccess::attachScript(*node, script);
	scripting::ScriptRuntime* rt = node->scriptRuntime();
	assert(rt != nullptr);

	// name-based round trip
	rt->setVar("health", std::any {55});
	assert(std::any_cast<int>(rt->getVar("health")) == 55);

	// path-based round trip into a group
	assert(rt->setVarByPath(0, "movement/accel", std::any {9.5}));
	assert(std::any_cast<double>(rt->getVarByPath(0, "movement/accel")) == 9.5);

	// writes to undeclared names are rejected
	assert(!rt->setVarByPath(0, "movement/nope", std::any {1}));

	// hot reload: swap the source in place; health survives (same name and kind),
	// speed disappears, armor arrives with its fresh default
	script->setData([] {
		constexpr std::string_view v2 = R"lua(
local M = {}
M.health = 100
M.armor = 5
M.movement = { accel = 4.0 }
return M
)lua";
		return std::vector<uint8_t>(v2.begin(), v2.end());
	}());
	node->reloadScripts();

	rt = node->scriptRuntime();
	assert(rt != nullptr);
	assert(std::any_cast<int>(rt->getVar("health")) == 55);
	assert(std::any_cast<double>(rt->getVarByPath(0, "movement/accel")) == 9.5);
	assert(std::any_cast<int>(rt->getVar("armor")) == 5);
	assert(!rt->getVar("speed").has_value());

	// collisions: the first script defining a name owns it for name-based access
	toast::_detail::WorldTestAccess::attachScript(*node, makeScript(R"lua(
local M = {}
M.health = 1
return M
)lua", 2));
	rt = node->scriptRuntime();
	rt->setVar("health", std::any {77});
	assert(std::any_cast<int>(rt->getVarByPath(0, "health")) == 77);
	assert(std::any_cast<int>(rt->getVarByPath(1, "health")) == 1);
}
