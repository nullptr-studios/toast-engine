#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <cassert>

using namespace toast::tests::scripting_tests;

// Two scripts that call each other forever overflow the native stack, which no pcall can catch. The depth guard ends
// the chain with an error long before that
TOAST_TEST_NAMED("Scripting", "scripting/10_call_loop_guard", test_scripting_10_call_loop_guard) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	auto first = toast::_detail::WorldTestAccess::createNode(*world_owner, "first");
	auto second = toast::_detail::WorldTestAccess::createNode(*world_owner, "second");

	constexpr const char* source = R"lua(
local M = {}
M.other = false
M.calls = 0
function M:ping()
    M.calls = M.calls + 1
    self.other:call("ping")
end
return M
)lua";
	toast::_detail::WorldTestAccess::attachScript(*first, makeScript(source, 1));
	toast::_detail::WorldTestAccess::attachScript(*second, makeScript(source, 2));
	first->scriptRuntime()->setVar("other", std::any {toast::Box<toast::Node>(second)});
	second->scriptRuntime()->setVar("other", std::any {toast::Box<toast::Node>(first)});

	first->call("ping");

	const int total =
	    std::any_cast<int>(first->scriptRuntime()->getVar("calls")) + std::any_cast<int>(second->scriptRuntime()->getVar("calls"));
	assert(total > 8);      // it did run for a while
	assert(total <= 60);    // and was cut off instead of recursing without end
}
