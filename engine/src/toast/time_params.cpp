#include "time_params.hpp"

namespace toast {

auto timeParams() -> TimeParams& {
	static TimeParams instance;
	return instance;
}

}
