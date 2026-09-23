#include "text_format.hpp"

#include "../log.hpp"

#include <vector>

namespace ui {

namespace {

void appendEscaped(std::string& out, char c) {
	switch (c) {
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '&': out += "&amp;"; break;
		default: out.push_back(c); break;
	}
}

void trim(std::string_view& s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.remove_prefix(1);
	}
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
		s.remove_suffix(1);
	}
}

auto styleForToken(std::string_view token, const TextFormatContext& ctx, std::string& out_style) -> bool {
	if (token == "bold" || token == "b") {
		out_style += "font-weight: bold;";
		return true;
	}
	if (token == "italic" || token == "i") {
		out_style += "font-style: italic;";
		return true;
	}
	if (token.starts_with("color:")) {
		std::string_view name = token.substr(6);
		trim(name);
		std::string color(name);
		if (ctx.color_resolver) {
			if (auto resolved = ctx.color_resolver(name)) {
				color = *resolved;
			}
		}
		out_style += "color: " + color + ";";
		return true;
	}
	return false;
}

void processGroup(std::string_view body, const TextFormatContext& ctx, std::string& out, int& open_spans) {
	// clear closes every open span
	if (body == "clear" || body == "c") {
		for (; open_spans > 0; open_spans--) {
			out += "</span>";
		}
		return;
	}

	std::string style;
	std::vector<std::string> data_vars;

	size_t start = 0;
	while (start <= body.size()) {
		const size_t comma = body.find(',', start);
		std::string_view token = body.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
		trim(token);

		if (!token.empty()) {
			if (token.starts_with("data:")) {
				std::string_view var = token.substr(5);
				trim(var);
				data_vars.emplace_back(var);
			} else if (!styleForToken(token, ctx, style)) {
				TOAST_WARN("UI", "Unknown text format directive '{}'", token);
			}
		}

		if (comma == std::string_view::npos) {
			break;
		}
		start = comma + 1;
	}

	if (!style.empty()) {
		out += "<span style=\"" + style + "\">";
		open_spans++;
	}
	for (const auto& var : data_vars) {
		out += "{{" + var + "}}";
	}
}

}

auto formatText(std::string_view input, const TextFormatContext& ctx) -> std::string {
	std::string out;
	out.reserve(input.size() + 16);
	int open_spans = 0;

	for (size_t i = 0; i < input.size(); i++) {
		const char c = input[i];
		if (c != '$') {
			appendEscaped(out, c);
			continue;
		}

		// Look at what follows the '$'
		if (i + 1 >= input.size()) {
			appendEscaped(out, c);
			continue;
		}

		const char next = input[i + 1];
		if (next == '$') {
			out.push_back('$');
			i++;
			continue;
		}
		if (next == 'c' || next == 'i' || next == 'b') {
			processGroup(std::string_view(&input[i + 1], 1), ctx, out, open_spans);
			i++;
			continue;
		}
		if (next == '{') {
			const size_t end = input.find('}', i + 2);
			if (end == std::string_view::npos) {
				// Unterminated group
				appendEscaped(out, c);
				continue;
			}
			processGroup(input.substr(i + 2, end - (i + 2)), ctx, out, open_spans);
			i = end;
			continue;
		}

		// A '$' stays literal
		appendEscaped(out, c);
	}

	for (; open_spans > 0; open_spans--) {
		out += "</span>";
	}

	return out;
}

auto formatText(std::string_view input) -> std::string {
	return formatText(input, TextFormatContext {});
}

}
