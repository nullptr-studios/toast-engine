#include "player.generated.hpp"
#include <toast/world/world.hpp>
#include <toast/log.hpp>
#include <array>

namespace toast {
void registerEngineTypes() {
	// One entry per reflected type; the generator appends to this list.
	static constexpr std::array all_types = {
		&Reflect<Node>::type_info,
	};

	for (const auto* info : all_types) {
		World::registerNode(info);
	}

	TOAST_INFO("Reflection", "Registered {} engine types", all_types.size());
}
}
