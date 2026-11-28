#include "input_params.hpp"

namespace input {

auto inputParams() -> InputParams& {
	static InputParams instance;
	return instance;
}

}
