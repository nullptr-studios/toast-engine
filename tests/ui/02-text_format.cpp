#include "toast/ui/text_format.hpp"

#include "test_registry.hpp"

#include <cassert>
#include <string>

TOAST_TEST_NAMED("ui", "ui/02-text_format", test_ui_02_text_format) {
	using ui::formatText;

	// Plain text passes through, with markup characters escaped
	assert(formatText("Hello") == "Hello");
	assert(formatText("a < b & c > d") == "a &lt; b &amp; c &gt; d");

	// Bold / italic open a span and auto-close at the end
	assert(formatText("${bold}hi") == "<span style=\"font-weight: bold;\">hi</span>");
	assert(formatText("$bhi") == "<span style=\"font-weight: bold;\">hi</span>");
	assert(formatText("${italic}x") == "<span style=\"font-style: italic;\">x</span>");
	assert(formatText("$ix") == "<span style=\"font-style: italic;\">x</span>");

	// clear closes an open span early
	assert(formatText("${bold}a${clear}b") == "<span style=\"font-weight: bold;\">a</span>b");
	assert(formatText("${bold}a$cb") == "<span style=\"font-weight: bold;\">a</span>b");

	// Combined directives collapse into one span
	assert(
	    formatText("${bold,italic}x") == "<span style=\"font-weight: bold;font-style: italic;\">x</span>"
	);

	// data:var becomes RmlUi binding syntax and is not escaped
	assert(formatText("HP: ${data:health}") == "HP: {{health}}");

	// $$ is a literal dollar; a lone $ stays literal
	assert(formatText("$$5") == "$5");
	assert(formatText("cost 5$") == "cost 5$");

	// Colors resolve through the scheme, else pass through literally
	ui::TextFormatContext ctx;
	ctx.color_resolver = [](std::string_view name) -> std::optional<std::string> {
		if (name == "danger") {
			return std::string("#ff0000ff");
		}
		return std::nullopt;
	};
	assert(formatText("${color:danger}x", ctx) == "<span style=\"color: #ff0000ff;\">x</span>");
	// Unknown scheme name falls back to a literal RCSS color
	assert(formatText("${color:green}x", ctx) == "<span style=\"color: green;\">x</span>");

	// Combined color + bold in one span
	assert(
	    formatText("${bold,color:danger}x", ctx) ==
	    "<span style=\"font-weight: bold;color: #ff0000ff;\">x</span>"
	);

	// Unknown directive is stripped, text survives
	assert(formatText("${wobble}x") == "x");
}
