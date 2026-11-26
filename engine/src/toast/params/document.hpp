/**
 * @file document.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief The common shape a config file has in memory
 */

#pragma once
#include <expected>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <toast/export.hpp>

namespace toast::params {

using Document = nlohmann::json;

TOAST_API auto tomlToDocument(std::string_view text) -> std::expected<Document, std::string>;

TOAST_API auto documentToToml(const Document& document) -> std::string;

}
