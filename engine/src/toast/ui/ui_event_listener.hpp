/**
 * @file ui_event_listener.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Routes inline `on*="Fn()"` handlers to node methods and Lua scripts
 */

#pragma once
#include <string>
#include <string_view>

namespace Rml {
class Context;
}

namespace ui {

[[nodiscard]]
auto parseCallName(std::string_view value) -> std::string;

/**
 * @brief Dispatches a UI event to the owning panel node's `call<void>(method)`
 *
 * Hits reflected C++ methods and attached Lua script with zero glue, so `function N:AcceptButton()`
 * in a panel script fires from the element `<button onclick="AcceptButton()">`
 */
void dispatchNodeCall(Rml::Context* context, std::string_view method);

/// @brief Registers the global event listener instancer with the RmlUi factory
void installEventListenerInstancer();

}
