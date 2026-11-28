#include "relative.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <vector>

namespace toast::console {
namespace {

using toast::params::Type;
using Result = std::expected<std::string, std::string>;

constexpr std::string_view k_whitespace = " \t";

auto trim(std::string_view text) -> std::string_view {
	const size_t first = text.find_first_not_of(k_whitespace);
	if (first == std::string_view::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(k_whitespace) - first + 1);
}

auto words(std::string_view text) -> std::vector<std::string_view> {
	std::vector<std::string_view> out;
	size_t at = 0;
	while (at < text.size()) {
		const size_t begin = text.find_first_not_of(k_whitespace, at);
		if (begin == std::string_view::npos) {
			break;
		}
		const size_t end = std::min(text.find_first_of(k_whitespace, begin), text.size());
		out.push_back(text.substr(begin, end - begin));
		at = end;
	}
	return out;
}

auto number(std::string_view text) -> std::optional<double> {
	if (text.starts_with('+')) {
		text.remove_prefix(1);
	}
	double value = 0.0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc {} || end != text.data() + text.size() || text.empty()) {
		return std::nullopt;
	}
	return value;
}

auto partsOf(Type type) -> size_t {
	switch (type) {
		case Type::vec2: return 2;
		case Type::vec3: return 3;
		case Type::vec4: return 4;
		default: return 1;
	}
}

auto part(std::string_view token, std::string_view current, bool whole) -> Result {
	if (token == "~") {
		return std::string {current};
	}
	if (!token.starts_with('~')) {
		return std::string {token};
	}
	if (token.size() < 2 || std::string_view {"+-*/"}.find(token[1]) == std::string_view::npos) {
		return std::unexpected(std::format("'{}': after ~ comes +, -, * or /", token));
	}
	const char op = token[1];
	const std::string_view operand_text = token.substr(2);
	if (operand_text.empty()) {
		return std::unexpected(std::format("'{}' needs a number after the {}", token, op));
	}
	const auto operand = number(operand_text);
	if (!operand) {
		return std::unexpected(std::format("'{}' is not a number", operand_text));
	}
	const auto base = number(current);
	if (!base) {
		return std::unexpected(std::format("'{}' is not a number to start from", current));
	}

	double result = 0.0;
	switch (op) {
		case '+': result = *base + *operand; break;
		case '-': result = *base - *operand; break;
		case '*': result = *base * *operand; break;
		default:
			if (*operand == 0.0) {
				return std::unexpected("it cannot be divided by zero");
			}
			result = *base / *operand;
			break;
	}
	if (whole) {
		return std::format("{}", static_cast<long long>(std::trunc(result)));
	}
	return std::format("{:.9g}", result);
}

}

auto hasRelative(std::string_view text) -> bool {
	bool quoted = false;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\' && quoted) {
			++i;
		} else if (text[i] == '"') {
			quoted = !quoted;
		} else if (text[i] == '~' && !quoted) {
			return true;
		}
	}
	return false;
}

auto resolveRelative(std::string_view typed, std::string_view current, Type type) -> Result {
	if (!hasRelative(typed)) {
		return std::string {typed};
	}
	const std::string_view text = trim(typed);

	switch (type) {
		case Type::boolean:
			if (text == "~") {
				return std::string {current};
			}
			if (text == "!~") {
				return std::string {current == "true" || current == "on" ? "false" : "true"};
			}
			return std::unexpected("a boolean takes ~ to repeat it or !~ to flip it");
		case Type::string:
		case Type::asset:
			if (text == "~") {
				return std::string {current};
			}
			return std::unexpected("a text can only repeat itself with ~");
		case Type::quat: return std::unexpected("a rotation cannot be written relative to itself");
		default: break;
	}

	if (text.contains("!~")) {
		return std::unexpected("only a boolean can be flipped with !~");
	}

	const size_t count = partsOf(type);
	const auto typed_parts = words(text);
	auto current_parts = words(current);
	if (type == Type::integer && count == 1 && text == "~") {
		return std::string {current};
	}
	if (current_parts.size() != count) {
		return std::unexpected(std::format("it has {} parts, not {}", current_parts.size(), count));
	}
	if (typed_parts.size() != 1 && typed_parts.size() != count) {
		return std::unexpected(std::format("expected {} values, got {}", count, typed_parts.size()));
	}

	const bool whole = type == Type::integer;
	std::string out;
	for (size_t i = 0; i < count; ++i) {
		const std::string_view token = typed_parts.size() == 1 ? typed_parts.front() : typed_parts[i];
		auto resolved = part(token, current_parts[i], whole);
		if (!resolved) {
			return resolved;
		}
		out += i == 0 ? "" : " ";
		out += *resolved;
	}
	return out;
}

}
