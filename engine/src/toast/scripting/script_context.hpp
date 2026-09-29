/**
 * @file script_context.hpp
 * @author Xein
 * @date 28 Sep 2026
 * @brief Tracks the node that owns the currently executing script
 */

#pragma once

#include <toast/world/box.hpp>

namespace toast {
class Node;
}

namespace scripting {

/// @returns the node owning the currently executing script
[[nodiscard]]
auto currentScriptNode() -> toast::Box<toast::Node>;

class ScriptNodeContextScope {
public:
	explicit ScriptNodeContextScope(toast::Box<toast::Node> node) noexcept;
	~ScriptNodeContextScope() noexcept;

	ScriptNodeContextScope(const ScriptNodeContextScope&) = delete;
	auto operator=(const ScriptNodeContextScope&) -> ScriptNodeContextScope& = delete;
	ScriptNodeContextScope(ScriptNodeContextScope&&) = delete;
	auto operator=(ScriptNodeContextScope&&) -> ScriptNodeContextScope& = delete;

private:
	toast::Box<toast::Node> m_previous;
};

}
