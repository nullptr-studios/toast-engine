#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace toast::tests::scripting_tests;
using scripting::LuaVarKind;

TOAST_TEST_NAMED("Scripting", "scripting/01_schema_extraction", test_scripting_01_schema_extraction) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto node = toast::_detail::WorldTestAccess::createNode(*world_owner, "host");

	constexpr std::string_view src = R"lua(
local M = {}

M.alive = true

---@name "Hit Points"
---@range 0 250
---@unit "hp"
M.health = 100

---@readonly
M.speed = 4.5
M.title = "hi"
M.dir = vec3(1, 0, 0)
M.tint = color3(1, 0, 0)
M.target = Node
M.names = { "a", "b" }

---@hidden
M._hidden = 3

---@hidden
M.internal = 7

M.movement = {
	---@range -10.5 20
	accel = 2.0,
	air = {
		---@unit '%'
		control = 0.5,
	},
}

function M:tick() end
function M:onSignal(value, count, ...) end

return M
)lua";
	toast::_detail::WorldTestAccess::attachScript(*node, makeScript(src));

	scripting::ScriptRuntime* rt = node->scriptRuntime();
	assert(rt != nullptr);
	assert(rt->instanceCount() == 1);
	const scripting::ScriptSchema* schema = rt->instanceSchema(0);
	assert(schema != nullptr);

	// declaration order preserved; functions and _-prefixed keys skipped
	std::vector<std::string> names;
	for (const auto& f : schema->fields) {
		names.push_back(f.name);
	}
	const std::vector<std::string> expected {
	  "alive", "health", "speed", "title", "dir", "tint", "target", "names", "internal"
	};
	assert(names == expected);

	assert(schema->find("alive")->kind == LuaVarKind::boolean);
	assert(schema->find("health")->kind == LuaVarKind::integer);
	assert(schema->find("speed")->kind == LuaVarKind::number);
	assert(schema->find("title")->kind == LuaVarKind::string);
	assert(schema->find("dir")->kind == LuaVarKind::vec3);
	assert(schema->find("tint")->kind == LuaVarKind::color3);
	assert(schema->find("target")->kind == LuaVarKind::node_ref);
	assert(schema->find("names")->kind == LuaVarKind::string);
	assert(schema->find("names")->is_array);
	assert(schema->find("_hidden") == nullptr);
	assert(schema->find("health")->display_name == "Hit Points");
	assert(schema->find("health")->min == 0.0);
	assert(schema->find("health")->max == 250.0);
	assert(schema->find("health")->unit == "hp");
	assert(schema->find("speed")->read_only);
	assert(schema->find("internal")->hidden);

	// nested tables: group with a leaf and a subgroup, addressable by slash path
	assert(schema->groups.size() == 1);
	assert(schema->groups[0].name == "movement");
	assert(schema->find("movement/accel") != nullptr);
	assert(schema->find("movement/accel")->kind == LuaVarKind::number);
	assert(schema->find("movement/accel")->min == -10.5);
	assert(schema->find("movement/accel")->max == 20.0);
	assert(schema->groups[0].subgroups.size() == 1);
	assert(schema->groups[0].subgroups[0].name == "air");
	assert(schema->find("movement/air/control") != nullptr);
	assert(schema->find("movement/air/control")->unit == "%");

	// the tick mask cache sees the tick() function and nothing else
	assert(rt->hasTick(toast::TickFunctionList::tick));
	assert(!rt->hasTick(toast::TickFunctionList::early_tick));

	const auto functions = rt->functions();
	const auto on_signal = std::ranges::find(functions, "onSignal", &scripting::LuaFunctionDesc::name);
	assert(on_signal != functions.end());
	assert(on_signal->parameters.size() == 2);
	assert(on_signal->is_vararg);
}
