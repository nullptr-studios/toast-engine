/**
 * @file signal_bindings.hpp
 * @author Xein
 * @date 7 Nov 2026
 */

#pragma once

#include <any>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <toast/events/signal_types.hpp>
#include <toast/export.hpp>
#include <typeinfo>
#include <vector>

namespace toast {
class Node;
}

namespace signals {

struct ResolvedBinding {
	bool from_signal = true;
	uint32_t signal_arg = 0;
	const std::type_info* param_type = nullptr;
	std::any value;
};

[[nodiscard]]
TOAST_API auto isBindableType(std::string_view cpp_type) -> bool;

[[nodiscard]]
TOAST_API auto literalToAny(const std::type_info& type, std::string_view literal) -> std::any;

[[nodiscard]]
TOAST_API auto literalToLuaAny(std::string_view lua_type, std::string_view literal) -> std::any;

/**
 * @brief Converts a value so it matches the exact type a parameter expects
 */
[[nodiscard]]
TOAST_API auto coerce(const std::any& value, const std::type_info& type) -> std::any;

[[nodiscard]]
TOAST_API auto resolveBindings(
    toast::Node& target, std::string_view function, std::span<const ParamBinding> bindings, size_t signal_arg_count
) -> std::optional<std::vector<ResolvedBinding>>;

/**
 * @brief Calls function on target with the arguments the bindings pick
 * @param signal_args the values the signal was fired with
 */
TOAST_API void invokeBound(
    toast::Node& target, std::string_view function, std::span<const ResolvedBinding> bindings,
    std::span<const std::any> signal_args
);

}
