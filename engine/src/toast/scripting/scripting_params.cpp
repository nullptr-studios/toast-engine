#include "scripting_params.hpp"

namespace scripting {

auto scriptingParams() -> ScriptingParams& {
	static ScriptingParams instance;
	return instance;
}

}
