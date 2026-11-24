#include "document_preprocess.hpp"

#include "ui_event_listener.hpp"

#include <cctype>

namespace ui {

namespace {

auto isIdentChar(char c) -> bool {
	return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

auto isExpressionKeyword(std::string_view word) -> bool {
	static constexpr std::array keywords {
	  "true", "false", "and", "or", "not", "it", "it_index", "it_size", "it_first", "it_last", "ev"
	};
	return std::ranges::find(keywords, word) != keywords.end();
}

void pushUnique(std::vector<std::string>& out, std::string value) {
	if (std::ranges::find(out, value) == out.end()) {
		out.push_back(std::move(value));
	}
}

void parseDataFor(std::string_view value, std::vector<std::string>& aliases, std::string_view& container) {
	const size_t colon = value.find(':');
	if (colon == std::string_view::npos) {
		container = {};
		return;
	}
	container = value.substr(colon + 1);

	std::string_view head = value.substr(0, colon);
	while (!head.empty()) {
		const size_t comma = head.find(',');
		std::string_view part = head.substr(0, comma);
		while (!part.empty() && (std::isspace(static_cast<unsigned char>(part.front())) != 0)) {
			part.remove_prefix(1);
		}
		while (!part.empty() && (std::isspace(static_cast<unsigned char>(part.back())) != 0)) {
			part.remove_suffix(1);
		}
		if (!part.empty()) {
			aliases.emplace_back(part);
		}
		head = comma == std::string_view::npos ? std::string_view {} : head.substr(comma + 1);
	}
}

auto asPlainIdentifier(std::string_view expression) -> std::string_view {
	while (!expression.empty() && (std::isspace(static_cast<unsigned char>(expression.front())) != 0)) {
		expression.remove_prefix(1);
	}
	while (!expression.empty() && (std::isspace(static_cast<unsigned char>(expression.back())) != 0)) {
		expression.remove_suffix(1);
	}
	if (expression.empty() || (std::isalpha(static_cast<unsigned char>(expression.front())) == 0 && expression.front() != '_')) {
		return {};
	}
	return std::ranges::all_of(expression, isIdentChar) ? expression : std::string_view {};
}

void collectIdentifiers(
    std::string_view expression, std::vector<std::string>& out, std::vector<std::string>* member_roots = nullptr
) {
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
				if (member_roots != nullptr && i < expression.size() && (expression[i] == '.' || expression[i] == '[')) {
					pushUnique(*member_roots, std::string(word));
				}
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
	std::string method = parseCallName(value);
	if (!method.empty()) {
		pushUnique(out, std::move(method));
	} else {
		TOAST_WARN("UI", "Ignoring invalid UI event call '{}'; expected Method()", value);
	}
}

auto findCaseInsensitive(std::string_view haystack, std::string_view needle, size_t offset = 0) -> size_t {
	// NOLINTNEXTLINE(readability-qualified-auto)
	const auto it = std::search(
	    haystack.begin() + static_cast<ptrdiff_t>(std::min(offset, haystack.size())),
	    haystack.end(),
	    needle.begin(),
	    needle.end(),
	    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }
	);
	return it == haystack.end() ? std::string_view::npos : static_cast<size_t>(it - haystack.begin());
}

auto attributeValue(std::string_view tag, std::string_view name) -> std::optional<std::string_view> {
	const size_t pos = findCaseInsensitive(tag, name);
	if (pos == std::string_view::npos) {
		return std::nullopt;
	}
	size_t cursor = pos + name.size();
	while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])) != 0) {
		cursor++;
	}
	if (cursor >= tag.size() || tag[cursor++] != '=') {
		return std::nullopt;
	}
	while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])) != 0) {
		cursor++;
	}
	if (cursor >= tag.size() || (tag[cursor] != '"' && tag[cursor] != '\'')) {
		return std::nullopt;
	}
	const char quote = tag[cursor++];
	const size_t end = tag.find(quote, cursor);
	return end == std::string_view::npos ? std::nullopt : std::optional(tag.substr(cursor, end - cursor));
}

auto normalizeStylesheetLinks(std::string source) -> std::string {
	size_t cursor = 0;
	while ((cursor = findCaseInsensitive(source, "<link", cursor)) != std::string::npos) {
		const size_t end = source.find('>', cursor);
		if (end == std::string::npos) {
			break;
		}
		const std::string_view tag(source.data() + cursor, end - cursor + 1);
		const auto type = attributeValue(tag, "type");
		if (!type || (*type != "text/css" && *type != "text/rcss")) {
			cursor = end + 1;
			continue;
		}
		const auto href = attributeValue(tag, "href");
		static constexpr std::string_view extension = ".rcss";
		const bool is_rcss = href && href->size() >= extension.size() &&
		                     std::ranges::equal(href->substr(href->size() - extension.size()), extension, [](char a, char b) {
			                     return std::tolower(static_cast<unsigned char>(a)) == b;
		                     });
		if (!is_rcss) {
			TOAST_WARN("UI", "Rejecting stylesheet link '{}': target must be an .rcss resource", href.value_or("<missing>"));
			source.erase(cursor, end - cursor + 1);
			continue;
		}
		if (*type == "text/rcss") {
			const size_t type_pos = findCaseInsensitive(source, "text/rcss", cursor);
			if (type_pos < end) {
				source.replace(type_pos, 9, "text/css");
			}
		}
		cursor = source.find('>', cursor) + 1;
	}
	return source;
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

namespace {

struct ScanState {
	DocumentScan& scan;
	std::vector<std::string> aliases;             // data-for loop variables, locals of the document
	std::vector<std::string> member_roots;        // identifiers read through . or [
	std::vector<std::string> array_candidates;    // plain containers of data-for
};

void scanMarkup(std::string_view rml, ScanState& state, std::vector<std::string>& template_links) {
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
			collectIdentifiers(rml.substr(i + 2, end - i - 2), state.scan.binds, &state.member_roots);
			i = end + 2;
			continue;
		}

		if (rml[i] == '<') {
			// scan attributes inside the tag
			const size_t tag_end = rml.find('>', i);
			const std::string_view tag = rml.substr(i, tag_end == std::string_view::npos ? rml.size() - i : tag_end - i);

			if (tag.size() > 5 && findCaseInsensitive(tag.substr(0, 5), "<link") == 0) {
				const auto type = attributeValue(tag, "type");
				const auto href = attributeValue(tag, "href");
				if (type && href && !href->empty() && *type == "text/template") {
					template_links.emplace_back(*href);
				}
			}

			size_t a = tag.find_first_of(" \t\r\n");
			if (a == std::string_view::npos) {
				i = tag_end == std::string_view::npos ? rml.size() : tag_end + 1;
				continue;
			}
			while (a < tag.size()) {
				// find attribute="value" pairs
				while (a < tag.size() && ((std::isspace(static_cast<unsigned char>(tag[a])) != 0) || tag[a] == '/')) {
					a++;
				}
				if (a >= tag.size()) {
					break;
				}
				const size_t name_start = a;
				while (a < tag.size() && tag[a] != '=' && tag[a] != '/' && (std::isspace(static_cast<unsigned char>(tag[a])) == 0)) {
					a++;
				}
				const std::string_view name = tag.substr(name_start, a - name_start);
				while (a < tag.size() && (std::isspace(static_cast<unsigned char>(tag[a])) != 0)) {
					a++;
				}
				if (name.empty() || a >= tag.size() || tag[a] != '=') {
					if (a < tag.size()) {
						a++;
					}
					continue;
				}
				a++;
				while (a < tag.size() && (std::isspace(static_cast<unsigned char>(tag[a])) != 0)) {
					a++;
				}
				if (a >= tag.size() || (tag[a] != '"' && tag[a] != '\'')) {
					continue;
				}
				const char quote = tag[a];
				a++;
				const size_t value_start = a;
				while (a < tag.size() && tag[a] != quote) {
					a++;
				}
				const std::string_view value = tag.substr(value_start, a - value_start);
				if (a < tag.size()) {
					a++;
				}

				if (name.starts_with("data-event-") || (name.starts_with("on") && value.contains('('))) {
					collectEventName(value, state.scan.events);
				} else if (name == "data-for") {
					// "item : items" or "item, index : items" binds the container
					std::string_view container;
					parseDataFor(value, state.aliases, container);
					if (!container.empty()) {
						collectIdentifiers(container, state.scan.binds, &state.member_roots);
						if (const auto plain = asPlainIdentifier(container); !plain.empty()) {
							pushUnique(state.array_candidates, std::string(plain));
						}
					}
				} else if (name.starts_with("data-") && name != "data-model" && !name.starts_with("data-loc-")) {
					collectIdentifiers(value, state.scan.binds, &state.member_roots);
					if (name == "data-checked") {
						collectIdentifiers(value, state.scan.bool_binds);
					}
				}
			}

			i = tag_end == std::string_view::npos ? rml.size() : tag_end + 1;
			continue;
		}

		i++;
	}
}

void scanTemplates(const PreprocessContext& ctx, ScanState& state, const std::vector<std::string>& document_links) {
	if (!ctx.template_loader) {
		return;
	}

	struct Pending {
		std::string referrer;
		std::string href;
	};

	std::vector<Pending> pending;
	pending.reserve(document_links.size());
	for (const auto& href : document_links) {
		pending.push_back({ctx.document_uri, href});
	}

	std::vector<std::string> visited {ctx.document_uri};
	for (size_t i = 0; i < pending.size(); ++i) {
		const Pending link = pending[i];    // copied, the vector grows below
		const auto loaded = ctx.template_loader(link.referrer, link.href);
		if (!loaded) {
			TOAST_WARN("UI", "Template '{}' linked from '{}' could not be read, its binds are not bound", link.href, link.referrer);
			continue;
		}
		if (std::ranges::find(visited, loaded->uri) != visited.end()) {
			continue;
		}
		visited.push_back(loaded->uri);

		std::vector<std::string> nested;
		scanMarkup(loaded->source, state, nested);
		for (auto& href : nested) {
			pending.push_back({loaded->uri, std::move(href)});
		}
	}
}

}

auto preprocessDocument(std::string_view rml, const PreprocessContext& ctx) -> DocumentScan {
	ZoneScoped;
	DocumentScan scan;
	ScanState state {.scan = scan};
	std::vector<std::string> template_links;
	scanMarkup(rml, state, template_links);
	scanTemplates(ctx, state, template_links);

	auto& aliases = state.aliases;
	auto& member_roots = state.member_roots;
	auto& array_candidates = state.array_candidates;

	// loop variables are locals of their `data-for`
	const auto is_alias = [&](const std::string& name) { return std::ranges::find(aliases, name) != aliases.end(); };
	std::erase_if(scan.binds, is_alias);
	std::erase_if(scan.bool_binds, is_alias);
	for (const auto& name : array_candidates) {
		if (!is_alias(name)) {
			pushUnique(scan.array_binds, name);
		}
	}
	for (const auto& name : member_roots) {
		if (!is_alias(name) && std::ranges::find(scan.array_binds, name) == scan.array_binds.end()) {
			pushUnique(scan.object_binds, name);
		}
	}

	// inject style links and the data model
	std::string result = normalizeStylesheetLinks(resolveColorReferences(rml, ctx.color_resolver));

	if (!ctx.style_uris.empty()) {
		std::string links;
		for (const auto& uri : ctx.style_uris) {
			if (!std::string_view(uri).ends_with(".rcss")) {
				TOAST_WARN("UI", "Rejecting global stylesheet '{}': target must end in .rcss", uri);
				continue;
			}
			links += std::format(R"(<link type="text/css" href="{}"/>)", uri);
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
			if (body_end != std::string::npos && !body_tag.contains("data-model")) {
				result.insert(body + 5, " data-model=\"binds\"");
			}
		}
	}

	scan.transformed_rml = std::move(result);
	return scan;
}

}
