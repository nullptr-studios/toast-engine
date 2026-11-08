#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <toast/uid.hpp>
#include <vector>

namespace signals {

enum class ConnectionSource : uint8_t {
	unknown,
	cpp,
	lua,
	editor,
};

struct DynamicArgs {
	std::vector<std::any> values;
};

struct ParamBinding {
	enum class Kind : uint8_t {
		signal_arg,
		literal,
	};

	Kind kind = Kind::signal_arg;
	uint32_t signal_arg = 0;
	std::string literal;

	auto operator==(const ParamBinding&) const -> bool = default;
};

struct ConnectionInfo {
	toast::UID target;
	std::string function;
	ConnectionSource source = ConnectionSource::unknown;
	bool forwards_args = true;
	std::vector<ParamBinding> bindings;
};

}
