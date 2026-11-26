#include "world_params.hpp"

namespace toast {

auto worldParams() -> WorldParams& {
	static WorldParams instance;
	return instance;
}

}
