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

struct ConnectionInfo {
	toast::UID target;
	std::string function;
	ConnectionSource source = ConnectionSource::unknown;
	bool forwards_args = true;
};

}
