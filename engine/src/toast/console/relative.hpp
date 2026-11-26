/**
 * @file relative.hpp
 * @author Xein
 * @date 25 Nov 2026
 *
 * @brief Values typed relative to what is there now
 */

#pragma once
#include <expected>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/params/value.hpp>

namespace toast::console {

[[nodiscard]]
TOAST_API auto hasRelative(std::string_view text) -> bool;

[[nodiscard]]
TOAST_API auto resolveRelative(std::string_view typed, std::string_view current, toast::params::Type type)
    -> std::expected<std::string, std::string>;

}
