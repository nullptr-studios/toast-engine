/**
 * @file script_context.hpp
 * @author Xein
 * @date 28 Sep 2026
 * @brief Tracks the script that is executing on the current thread
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <toast/world/box.hpp>

namespace toast {
class Node;
}

namespace scripting {

/**
 * @brief Lifetime marker of one ScriptRuntime
 *
 * Everything C++ keeps that points into a runtime's scripts (state machine callbacks, deferred calls,
 * event subscriptions) holds the token and checks it before calling. It outlives the runtime
 * and flips to dead as the runtime is torn down, so a stale callback is skipped instead of run
 */
struct RuntimeToken {
	std::atomic<bool> alive {true};
	uint32_t id = 0;
	size_t vm_index = 0;
};

/// The script that is executing on the current thread
struct ScriptContext {
	toast::Box<toast::Node> node;
	std::shared_ptr<RuntimeToken> token;
};

/// @returns the node owning the currently executing script
[[nodiscard]]
auto currentScriptNode() -> toast::Box<toast::Node>;

[[nodiscard]]
auto currentScriptContext() -> const ScriptContext&;

class ScriptNodeContextScope {
public:
	explicit ScriptNodeContextScope(toast::Box<toast::Node> node, std::shared_ptr<RuntimeToken> token = {}) noexcept;
	~ScriptNodeContextScope() noexcept;

	ScriptNodeContextScope(const ScriptNodeContextScope&) = delete;
	auto operator=(const ScriptNodeContextScope&) -> ScriptNodeContextScope& = delete;
	ScriptNodeContextScope(ScriptNodeContextScope&&) = delete;
	auto operator=(ScriptNodeContextScope&&) -> ScriptNodeContextScope& = delete;

private:
	ScriptContext m_previous;
};

}
