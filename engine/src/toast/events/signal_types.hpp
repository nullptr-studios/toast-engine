#pragma once

#include <cstdint>
#include <string>
#include <toast/uid.hpp>

namespace signals {

enum class ConnectionSource : uint8_t {
	unknown,
	cpp,
	lua,
	editor,
};

struct ConnectionInfo {
	toast::UID target;
	std::string function;
	ConnectionSource source = ConnectionSource::unknown;
	bool forwards_args = true;
};

}
