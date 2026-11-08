#include "signal_bindings.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/scripting/script_runtime.hpp>
#include <toast/world/node.hpp>

namespace signals {
namespace {

// boilerplate: the file (TM)
// enjoy this shit ass file

template<typename T>
auto parseNumber(std::string_view text) -> std::optional<T> {
	while (!text.empty() && text.front() == ' ') {
		text.remove_prefix(1);
	}
	while (!text.empty() && text.back() == ' ') {
		text.remove_suffix(1);
	}
	T value {};
	if constexpr (std::is_floating_point_v<T>) {
		std::string copy(text);
		char* end = nullptr;
		const double parsed = std::strtod(copy.c_str(), &end);
		if (copy.empty() || end != copy.c_str() + copy.size()) {
			return std::nullopt;
		}
		return static_cast<T>(parsed);
	} else {
		auto [next, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (error != std::errc {} || next != text.data() + text.size()) {
			return std::nullopt;
		}
		return value;
	}
}

template<size_t N>
auto parseFloats(std::string_view text) -> std::optional<std::array<float, N>> {
	std::array<float, N> result {};
	for (size_t i = 0; i < N; ++i) {
		while (!text.empty() && text.front() == ' ') {
			text.remove_prefix(1);
		}
		const size_t end = text.find(' ');
		auto value = parseNumber<float>(text.substr(0, end));
		if (!value) {
			return std::nullopt;
		}
		result[i] = *value;
		text = end == std::string_view::npos ? std::string_view {} : text.substr(end);
	}
	return result;
}

template<typename T>
auto integerAny(std::string_view literal) -> std::any {
	if (auto value = parseNumber<T>(literal)) {
		return std::any {*value};
	}
	return {};
}

template<typename From>
auto convertFrom(const From& value, const std::type_info& type) -> std::any {
	if (type == typeid(bool)) {
		return static_cast<bool>(value);
	}
	if (type == typeid(int8_t)) {
		return static_cast<int8_t>(value);
	}
	if (type == typeid(int16_t)) {
		return static_cast<int16_t>(value);
	}
	if (type == typeid(int32_t)) {
		return static_cast<int32_t>(value);
	}
	if (type == typeid(int64_t)) {
		return static_cast<int64_t>(value);
	}
	if (type == typeid(uint8_t)) {
		return static_cast<uint8_t>(value);
	}
	if (type == typeid(uint16_t)) {
		return static_cast<uint16_t>(value);
	}
	if (type == typeid(uint32_t)) {
		return static_cast<uint32_t>(value);
	}
	if (type == typeid(uint64_t)) {
		return static_cast<uint64_t>(value);
	}
	if (type == typeid(float)) {
		return static_cast<float>(value);
	}
	if (type == typeid(double)) {
		return static_cast<double>(value);
	}
	return {};
}

}

auto isBindableType(std::string_view cpp_type) -> bool {
	for (const std::string_view container : {"vector<", "array<", "span<", "list<", "map<", "set<", "deque<", "[]"}) {
		if (cpp_type.contains(container)) {
			return false;
		}
	}
	return true;
}

auto literalToAny(const std::type_info& type, std::string_view literal) -> std::any {
	if (type == typeid(bool)) {
		if (literal == "true" || literal == "1") {
			return true;
		}
		if (literal == "false" || literal == "0") {
			return false;
		}
		return {};
	}
	if (type == typeid(int8_t)) {
		return integerAny<int8_t>(literal);
	}
	if (type == typeid(int16_t)) {
		return integerAny<int16_t>(literal);
	}
	if (type == typeid(int32_t)) {
		return integerAny<int32_t>(literal);
	}
	if (type == typeid(int64_t)) {
		return integerAny<int64_t>(literal);
	}
	if (type == typeid(uint8_t)) {
		return integerAny<uint8_t>(literal);
	}
	if (type == typeid(uint16_t)) {
		return integerAny<uint16_t>(literal);
	}
	if (type == typeid(uint32_t)) {
		return integerAny<uint32_t>(literal);
	}
	if (type == typeid(uint64_t)) {
		return integerAny<uint64_t>(literal);
	}
	if (type == typeid(float)) {
		return integerAny<float>(literal);
	}
	if (type == typeid(double)) {
		return integerAny<double>(literal);
	}
	if (type == typeid(std::string)) {
		return std::string(literal);
	}
	if (type == typeid(glm::vec2)) {
		if (auto v = parseFloats<2>(literal)) {
			return glm::vec2 {(*v)[0], (*v)[1]};
		}
		return {};
	}
	if (type == typeid(glm::vec3)) {
		if (auto v = parseFloats<3>(literal)) {
			return glm::vec3 {(*v)[0], (*v)[1], (*v)[2]};
		}
		return {};
	}
	if (type == typeid(glm::vec4)) {
		if (auto v = parseFloats<4>(literal)) {
			return glm::vec4 {(*v)[0], (*v)[1], (*v)[2], (*v)[3]};
		}
		return {};
	}
	return {};
}

auto literalToLuaAny(std::string_view lua_type, std::string_view literal) -> std::any {
	if (lua_type == "any" && literal.size() >= 2 && literal[1] == ':') {
		const std::string_view value = literal.substr(2);
		switch (literal[0]) {
			case 'i': return literalToLuaAny("integer", value);
			case 'n': return literalToLuaAny("number", value);
			case 's': return literalToLuaAny("string", value);
			case 'b': return literalToLuaAny("boolean", value);
			default: return {};
		}
	}
	if (lua_type == "integer") {
		return integerAny<int64_t>(literal);
	}
	if (lua_type == "number") {
		return integerAny<double>(literal);
	}
	if (lua_type == "string") {
		return std::string(literal);
	}
	if (lua_type == "boolean") {
		if (literal == "true") {
			return true;
		}
		if (literal == "false") {
			return false;
		}
	}
	return {};
}

auto coerce(const std::any& value, const std::type_info& type) -> std::any {
	if (!value.has_value()) {
		return {};
	}
	if (value.type() == type) {
		return value;
	}
	if (const auto* v = std::any_cast<bool>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<int8_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<int16_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<int32_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<int64_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<uint8_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<uint16_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<uint32_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<uint64_t>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<float>(&value)) {
		return convertFrom(*v, type);
	}
	if (const auto* v = std::any_cast<double>(&value)) {
		return convertFrom(*v, type);
	}
	return {};
}

auto resolveBindings(
    toast::Node& target, std::string_view function, std::span<const ParamBinding> bindings, size_t signal_arg_count
) -> std::optional<std::vector<ResolvedBinding>> {
	std::vector<ResolvedBinding> result;
	result.reserve(bindings.size());

	const toast::FunctionInfo* method = target.info() ? target.info()->getMethod(function) : nullptr;
	std::vector<std::string> lua_types;
	if (!method) {
		if (auto* runtime = target.scriptRuntime()) {
			for (const auto& description : runtime->functions()) {
				if (description.name == function) {
					lua_types = description.parameter_types;
					lua_types.resize(description.parameters.size(), "any");
					break;
				}
			}
		}
	}
	const size_t expected = method ? method->parameters.size() : lua_types.size();
	if (bindings.size() != expected) {
		return std::nullopt;
	}

	for (size_t i = 0; i < bindings.size(); ++i) {
		ResolvedBinding resolved;
		resolved.param_type = method ? method->parameters[i].type_id : nullptr;
		if (bindings[i].kind == ParamBinding::Kind::signal_arg) {
			if (bindings[i].signal_arg >= signal_arg_count) {
				return std::nullopt;
			}
			resolved.signal_arg = bindings[i].signal_arg;
		} else {
			resolved.from_signal = false;
			if (method) {
				if (!resolved.param_type) {
					return std::nullopt;
				}
				resolved.value = literalToAny(*resolved.param_type, bindings[i].literal);
			} else {
				resolved.value = literalToLuaAny(lua_types[i], bindings[i].literal);
			}
			if (!resolved.value.has_value()) {
				return std::nullopt;
			}
		}
		result.push_back(std::move(resolved));
	}
	return result;
}

void invokeBound(
    toast::Node& target, std::string_view function, std::span<const ResolvedBinding> bindings,
    std::span<const std::any> signal_args
) {
	std::vector<std::any> args;
	args.reserve(bindings.size());
	for (const auto& binding : bindings) {
		if (!binding.from_signal) {
			args.push_back(binding.value);
			continue;
		}
		if (binding.signal_arg >= signal_args.size()) {
			return;
		}
		const std::any& value = signal_args[binding.signal_arg];
		// the reflected invoker casts to the exact parameter type, so a mismatch has to be fixed here
		args.push_back(binding.param_type ? coerce(value, *binding.param_type) : value);
		if (binding.param_type && !args.back().has_value()) {
			return;
		}
	}

	if (const auto* info = target.info(); info && info->getMethod(function)) {
		(void)info->callAllDynamic(&target, function, args);
	}
	toast::_detail::callNodeScripts(&target, function, args);
}

}
