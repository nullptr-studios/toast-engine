#include "text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace toast::params {
namespace {

using Parsed = std::expected<Scalar, std::string>;

auto trim(std::string_view text) -> std::string_view {
	constexpr std::string_view k_space = " \t\r\n";
	const size_t first = text.find_first_not_of(k_space);
	if (first == std::string_view::npos) {
		return {};
	}
	return text.substr(first, text.find_last_not_of(k_space) - first + 1);
}

auto sameIgnoringCase(std::string_view a, std::string_view b) -> bool {
	return std::ranges::equal(a, b, [](char x, char y) {
		return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	});
}

auto parseDouble(std::string_view token) -> std::expected<double, std::string> {
	double value = 0.0;
	const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
	if (token.empty() || error != std::errc {} || end != token.data() + token.size() || !std::isfinite(value)) {
		return std::unexpected(std::format("expected a number, got '{}'", token));
	}
	return value;
}

auto parseInteger(std::string_view token, std::span<const std::string_view> options) -> Parsed {
	for (size_t i = 0; i < options.size(); ++i) {
		if (sameIgnoringCase(token, options[i])) {
			return Scalar {static_cast<int64_t>(i)};
		}
	}

	int64_t value = 0;
	const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
	const bool is_number = !token.empty() && error == std::errc {} && end == token.data() + token.size();

	if (options.empty()) {
		if (!is_number) {
			return std::unexpected(std::format("expected a whole number, got '{}'", token));
		}
		return Scalar {value};
	}

	if (!is_number || value < 0 || static_cast<size_t>(value) >= options.size()) {
		std::string choices;
		for (const auto& option : options) {
			choices += choices.empty() ? "" : ", ";
			choices += option;
		}
		return std::unexpected(std::format("'{}' is not one of: {}", token, choices));
	}
	return Scalar {value};
}

auto parseComponents(std::string_view token, size_t count) -> std::expected<std::array<float, 4>, std::string> {
	std::array<float, 4> out {};
	size_t found = 0;
	size_t pos = 0;
	while (true) {
		pos = token.find_first_not_of(" \t", pos);
		if (pos == std::string_view::npos) {
			break;
		}
		const size_t end = token.find_first_of(" \t", pos);
		const auto part = token.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
		auto number = parseDouble(part);
		if (!number) {
			return std::unexpected(number.error());
		}
		if (found < out.size()) {
			out[found] = static_cast<float>(*number);
		}
		++found;
		if (end == std::string_view::npos) {
			break;
		}
		pos = end;
	}
	if (found != count) {
		return std::unexpected(std::format("expected {} numbers, got {}", count, found));
	}
	return out;
}

auto parseString(std::string_view token) -> Parsed {
	if (token.empty() || token.front() != '"') {
		return Scalar {std::string {token}};
	}
	std::string out;
	for (size_t i = 1; i < token.size(); ++i) {
		const char c = token[i];
		if (c == '\\' && i + 1 < token.size() && (token[i + 1] == '"' || token[i + 1] == '\\')) {
			out += token[++i];
		} else if (c == '"') {
			if (i + 1 != token.size()) {
				return std::unexpected("text after the closing quote");
			}
			return Scalar {std::move(out)};
		} else {
			out += c;
		}
	}
	return std::unexpected("missing closing quote");
}

auto parseScalar(std::string_view raw, Type type, std::span<const std::string_view> options) -> Parsed {
	const std::string_view token = trim(raw);
	switch (type) {
		case Type::boolean:
			if (token == "true" || token == "on") {
				return Scalar {true};
			}
			if (token == "false" || token == "off") {
				return Scalar {false};
			}
			return std::unexpected(std::format("expected true, false, on or off, got '{}'", token));
		case Type::integer: return parseInteger(token, options);
		case Type::floating: {
			auto number = parseDouble(token);
			if (!number) {
				return std::unexpected(number.error());
			}
			return Scalar {*number};
		}
		case Type::string: return parseString(token);
		case Type::vec2:
		case Type::vec3:
		case Type::vec4:
		case Type::quat: {
			const size_t count = type == Type::vec2 ? 2 : (type == Type::vec3 ? 3 : 4);
			auto c = parseComponents(token, count);
			if (!c) {
				return std::unexpected(c.error());
			}
			switch (type) {
				case Type::vec2:
					return Scalar {
					  glm::vec2 {(*c)[0], (*c)[1]}
					};
				case Type::vec3:
					return Scalar {
					  glm::vec3 {(*c)[0], (*c)[1], (*c)[2]}
					};
				case Type::vec4:
					return Scalar {
					  glm::vec4 {(*c)[0], (*c)[1], (*c)[2], (*c)[3]}
					};
				default:
					return Scalar {
					  glm::quat {(*c)[3], (*c)[0], (*c)[1], (*c)[2]}
					};
			}
		}
		case Type::asset:
			if (!token.empty() && (token.find("://") == std::string_view::npos || token.find(' ') != std::string_view::npos)) {
				return std::unexpected(std::format("expected a URI like asset://name, got '{}'", token));
			}
			return Scalar {AssetRef {std::string {token}}};
	}
	return std::unexpected("unknown type");
}

auto splitElements(std::string_view text) -> std::vector<std::string_view> {
	std::vector<std::string_view> parts;
	size_t start = 0;
	bool quoted = false;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\' && quoted) {
			++i;
		} else if (text[i] == '"') {
			quoted = !quoted;
		} else if (text[i] == ';' && !quoted) {
			parts.push_back(text.substr(start, i - start));
			start = i + 1;
		}
	}
	parts.push_back(text.substr(start));
	return parts;
}

auto formatFloat(double value) -> std::string {
	std::array<char, 64> buffer {};
	const auto narrowed = static_cast<float>(value);
	const auto result = static_cast<double>(narrowed) == value
	                        ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), narrowed)
	                        : std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	return {buffer.data(), result.ptr};
}

auto quote(const std::string& text) -> std::string {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') {
			out += '\\';
		}
		out += c;
	}
	out += '"';
	return out;
}

auto scalarToText(const Scalar& scalar, std::span<const std::string_view> options) -> std::string {
	return std::visit(
	    [&](const auto& v) -> std::string {
		    using T = std::decay_t<decltype(v)>;
		    if constexpr (std::is_same_v<T, bool>) {
			    return v ? "true" : "false";
		    } else if constexpr (std::is_same_v<T, int64_t>) {
			    if (v >= 0 && static_cast<size_t>(v) < options.size()) {
				    return std::string {options[static_cast<size_t>(v)]};
			    }
			    return std::to_string(v);
		    } else if constexpr (std::is_same_v<T, double>) {
			    return formatFloat(v);
		    } else if constexpr (std::is_same_v<T, std::string>) {
			    return quote(v);
		    } else if constexpr (std::is_same_v<T, glm::vec2>) {
			    return formatFloat(v.x) + " " + formatFloat(v.y);
		    } else if constexpr (std::is_same_v<T, glm::vec3>) {
			    return formatFloat(v.x) + " " + formatFloat(v.y) + " " + formatFloat(v.z);
		    } else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>) {
			    return formatFloat(v.x) + " " + formatFloat(v.y) + " " + formatFloat(v.z) + " " + formatFloat(v.w);
		    } else {
			    return v.uri;
		    }
	    },
	    scalar
	);
}

}

auto fromText(std::string_view text, Type type, bool is_array, std::span<const std::string_view> options)
    -> std::expected<Value, std::string> {
	if (!is_array) {
		auto scalar = parseScalar(text, type, options);
		if (!scalar) {
			return std::unexpected(scalar.error());
		}
		return Value {std::move(*scalar)};
	}

	std::vector<Scalar> items;
	if (trim(text).empty()) {
		return Value {std::move(items)};
	}
	const auto parts = splitElements(text);
	for (size_t i = 0; i < parts.size(); ++i) {
		if (trim(parts[i]).empty()) {
			return std::unexpected(std::format("element {} is empty", i + 1));
		}
		auto scalar = parseScalar(parts[i], type, options);
		if (!scalar) {
			return std::unexpected(std::format("element {}: {}", i + 1, scalar.error()));
		}
		items.push_back(std::move(*scalar));
	}
	return Value {std::move(items)};
}

auto toText(const Value& value, Type type, std::span<const std::string_view> options) -> std::string {
	const auto choices = type == Type::integer ? options : std::span<const std::string_view> {};
	if (const auto* scalar = std::get_if<Scalar>(&value)) {
		return scalarToText(*scalar, choices);
	}
	std::string out;
	bool first = true;
	for (const auto& item : std::get<std::vector<Scalar>>(value)) {
		if (!first) {
			out += ';';
		}
		first = false;
		out += scalarToText(item, choices);
	}
	return out;
}

}
