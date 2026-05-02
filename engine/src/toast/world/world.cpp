#include "world.hpp"

#include <memory>

namespace toast {
auto World::create() noexcept -> std::unique_ptr<World> {
	struct Helper : public World { };

	auto ptr = std::make_unique<Helper>();
	instance = ptr.get();
	return ptr;
}
}
