#include "script_context.hpp"

#include <toast/world/node.hpp>

namespace scripting {

namespace {
thread_local ScriptContext t_current_script;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}

auto currentScriptNode() -> toast::Box<toast::Node> {
	return t_current_script.node;
}

auto currentScriptContext() -> const ScriptContext& {
	return t_current_script;
}

ScriptNodeContextScope::ScriptNodeContextScope(toast::Box<toast::Node> node, std::shared_ptr<RuntimeToken> token) noexcept
    : m_previous(std::move(t_current_script)) {
	t_current_script = ScriptContext {.node = std::move(node), .token = std::move(token)};
}

ScriptNodeContextScope::~ScriptNodeContextScope() noexcept {
	t_current_script = std::move(m_previous);
}

}
