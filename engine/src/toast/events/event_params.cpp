#include "event_params.hpp"

namespace event {

auto eventParams() -> EventParams& {
	static EventParams instance;
	return instance;
}

}
