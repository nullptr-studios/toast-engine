#include "document_preprocess.hpp"

#include "../log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace ui {

namespace {

auto isIdentChar(char c) -> bool {
	return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

auto isExpressionKeyword(std::string_view word) -> bool {
	static constexpr std::array keywords {"true", "false", "and", "or", "not", "it", "it_index", "ev"};
	return std::ranges::find(keywords, word) != keywords.end();
}

void pushUnique(std::vector<std::string>& out, std::string value) {
	if (std::ranges::find(out, value) == out.end()) {
		out.push_back(std::move(value));
	}
}

void collectIdentifiers(std::string_view expression, std::vector<std::string>& out) {
	size_t i = 0;
	while (i < expression.size()) {
		const char c = expression[i];
		if (c == '\'') {
			// skip string literals
			i++;
			while (i < expression.size() && expression[i] != '\'') {
				i++;
			}
			i++;
			continue;
		}

		if ((std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_') {
			const size_t start = i;
			while (i < expression.size() && isIdentChar(expression[i])) {
				i++;
			}
			// member accesses like item.name bind the root identifier only
			std::string_view word = expression.substr(start, i - start);
			if (i < expression.size() && expression[i] == '(') {
				continue;    // function call
			}
			if (!isExpressionKeyword(word) && (std::isdigit(static_cast<unsigned char>(word[0])) == 0)) {
				pushUnique(out, std::string(word));
			}
			// skip the member chain
			while (i < expression.size() && (isIdentChar(expression[i]) || expression[i] == '.')) {
				i++;
			}
			continue;
		}

		i++;
	}
}

void collectEventName(std::string_view value, std::vector<std::string>& out) {
	size_t start = 0;
	while (start < value.size() && (std::isspace(static_cast<unsigned char>(value[start])) != 0)) {
		start++;
	}
	size_t end = start;
	while (end < value.size() && isIdentChar(value[end])) {
		end++;
	}
	if (end > start) {
		pushUnique(out, std::string(value.substr(start, end - start)));
	}
}

auto findCaseInsensitive(std::string_view haystack, std::string_view needle, size_t offset = 0) -> size_t {
	const auto it = std::search(
	    haystack.begin() + static_cast<ptrdiff_t>(std::min(offset, haystack.size())),
	    haystack.end(),
	    needle.begin(),
	    needle.end(),
	    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }
	);
	return it == haystack.end() ? std::string_view::npos : static_cast<size_t>(it - haystack.begin());
}

}

auto resolveColorReferences(std::string_view source, const ColorResolver& resolver) -> std::string {
	if (!resolver) {
		return std::string(source);
	}

	std::string result;
	result.reserve(source.size());

	size_t cursor = 0;
	while (cursor < source.size()) {
		const size_t marker = source.find("${color:", cursor);
		if (marker == std::string_view::npos) {
			result.append(source.substr(cursor));
			break;
		}

		result.append(source.substr(cursor, marker - cursor));
		const size_t name_start = marker + 8;
		const size_t end = source.find('}', name_start);
		if (end == std::string_view::npos) {
			result.append(source.substr(marker));
			break;
		}

		const std::string_view name = source.substr(name_start, end - name_start);
		if (name.empty()) {
			result.append(source.substr(marker, end - marker + 1));
		} else if (auto color = resolver(name)) {
			result.append(*color);
		} else {
			result.append(name);
		}
		cursor = end + 1;
	}

	return result;
}

auto preprocessDocument(std::string_view rml, const PreprocessContext& ctx) -> DocumentScan {
	DocumentScan scan;

	// can attributes and {{ }} expressions
	size_t i = 0;
	while (i < rml.size()) {
		if (rml.substr(i).starts_with("<!--")) {
			const size_t end = rml.find("-->", i);
			i = end == std::string_view::npos ? rml.size() : end + 3;
			continue;
		}

		if (rml.substr(i).starts_with("{{")) {
			const size_t end = rml.find("}}", i);
			if (end == std::string_view::npos) {
				break;
			}
			collectIdentifiers(rml.substr(i + 2, end - i - 2), scan.binds);
			i = end + 2;
			continue;
		}

		if (rml[i] == '<') {
			// scan attributes inside the tag
			const size_t tag_end = rml.find('>', i);
			const std::string_view tag = rml.substr(i, tag_end == std::string_view::npos ? rml.size() - i : tag_end - i);

			size_t a = 0;
			while (a < tag.size()) {
				// find attribute="value" pairs
				while (a < tag.size() && (std::isspace(static_cast<unsigned char>(tag[a])) == 0)) {
					a++;
				}
				while (a < tag.size() && (std::isspace(static_cast<unsigned char>(tag[a])) != 0)) {
					a++;
				}
				const size_t name_start = a;
				while (a < tag.size() && tag[a] != '=' && (std::isspace(static_cast<unsigned char>(tag[a])) == 0)) {
					a++;
				}
				const std::string_view name = tag.substr(name_start, a - name_start);
				if (a >= tag.size() || tag[a] != '=') {
					continue;
				}
				a++;
				if (a >= tag.size() || tag[a] != '"') {
					continue;
				}
				a++;
				const size_t value_start = a;
				while (a < tag.size() && tag[a] != '"') {
					a++;
				}
				const std::string_view value = tag.substr(value_start, a - value_start);
				a++;

				if (name.starts_with("data-event-")) {
					collectEventName(value, scan.events);
				} else if (name.starts_with("on") && value.find('(') != std::string_view::npos) {
					collectEventName(value, scan.events);
				} else if (name == "data-for") {
					// "item : items" or "item, index : items" binds the container
					const size_t colon = value.find(':');
					if (colon != std::string_view::npos) {
						collectIdentifiers(value.substr(colon + 1), scan.binds);
					}
				} else if (name.starts_with("data-") && name != "data-model" && !name.starts_with("data-loc-")) {
					collectIdentifiers(value, scan.binds);
					if (name == "data-checked") {
						collectIdentifiers(value, scan.bool_binds);
					}
				}
			}

			i = tag_end == std::string_view::npos ? rml.size() : tag_end + 1;
			continue;
		}

		i++;
	}

	// inject style links and the data model
	std::string result = resolveColorReferences(rml, ctx.color_resolver);

	if (!ctx.style_uris.empty()) {
		std::string links;
		for (const auto& uri : ctx.style_uris) {
			links += std::format("<link type=\"text/rcss\" href=\"{}\"/>", uri);
		}

		const size_t head = findCaseInsensitive(result, "<head>");
		if (head != std::string::npos) {
			result.insert(head + 6, links);
		} else {
			const size_t root = findCaseInsensitive(result, "<rml>");
			if (root != std::string::npos) {
				result.insert(root + 5, "<head>" + links + "</head>");
			} else {
				TOAST_WARN("UI", "Document has no <head> or <rml> tag; style sheets not injected");
			}
		}
	}

	if (ctx.inject_data_model && !(scan.binds.empty() && scan.events.empty())) {
		const size_t body = findCaseInsensitive(result, "<body");
		if (body != std::string::npos) {
			const size_t body_end = result.find('>', body);
			const std::string_view body_tag = std::string_view(result).substr(body, body_end - body);
			if (body_end != std::string::npos && body_tag.find("data-model") == std::string_view::npos) {
				result.insert(body + 5, " data-model=\"binds\"");
			}
		}
	}

	scan.transformed_rml = std::move(result);
	return scan;
}

}
