#include "script_context.hpp"

#include <toast/world/node.hpp>

namespace scripting {

namespace {
thread_local toast::Box<toast::Node> t_current_script_node;
}

auto currentScriptNode() -> toast::Box<toast::Node> {
	return t_current_script_node;
}

ScriptNodeContextScope::ScriptNodeContextScope(toast::Box<toast::Node> node) noexcept : m_previous(t_current_script_node) {
	t_current_script_node = std::move(node);
}

ScriptNodeContextScope::~ScriptNodeContextScope() noexcept {
	t_current_script_node = std::move(m_previous);
}

}
