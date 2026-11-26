#include "physics_settings.hpp"

#include "layers.hpp"

namespace physics {

namespace {

auto instance() -> Tunables& {
	static Tunables params;
	return params;
}

}

auto tunables() -> const Tunables& {
	return instance();
}

void registerPhysicsSettings() {
	registerLayerSettings(instance());
}

}
