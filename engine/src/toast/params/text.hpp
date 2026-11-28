/**
 * @file text.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Values as the text a person types and reads
 */

#pragma once
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/params/value.hpp>

namespace toast::params {

TOAST_API auto fromText(std::string_view text, Type type, bool is_array, std::span<const std::string_view> options = {})
    -> std::expected<Value, std::string>;

TOAST_API auto toText(const Value& value, Type type, std::span<const std::string_view> options = {}) -> std::string;

}
