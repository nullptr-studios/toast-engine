#include "player.generated.hpp"
#include <toast/world/world.hpp>
#include <toast/log.hpp>

namespace toast {
void registerEngineTypes() {
	World::registerNode(&Reflect<Node>::type_info);

	TOAST_INFO("Reflection", "Registered 1 engine types");
}
}

