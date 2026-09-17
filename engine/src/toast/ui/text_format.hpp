/**
 * @file text_format.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief `${}` inline formatting for localized text
 */

#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <toast/export.hpp>

namespace ui {

struct TextFormatContext {
	std::function<std::optional<std::string>(std::string_view)> color_resolver;
};

/**
 * @brief Expands `${}` directives in localized text into RML `<span>` markup
 *
 * Supported directives:
 * - `${clear}` / `$c`   close all open styling spans
 * - `${italic}` / `$i`  italic span
 * - `${bold}` / `$b`    bold span
 * - `${color:name}`     colored span (scheme name, else literal color)
 * - `${bold,color:red}` combine styles in one span (comma-separated)
 * - `${data:var}`       data binding, becomes `{{var}}`
 * - `$$`                a literal `$`
 */
[[nodiscard]]
TOAST_API auto formatText(std::string_view input, const TextFormatContext& ctx) -> std::string;

[[nodiscard]]
TOAST_API auto formatText(std::string_view input) -> std::string;

}
