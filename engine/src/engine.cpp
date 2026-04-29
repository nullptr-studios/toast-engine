#include "engine.hpp"

#include "ffi/engine.h"    // ffi
#include "logger.hpp"
#include "thread_pool.hpp"

#include <cassert>
#include <memory>

namespace toast {

Engine* Engine::instance = nullptr;

struct EnginePimpl {
	std::unique_ptr<ThreadPool> thread_pool;
	std::unique_ptr<logging::Logger> logger;
};

Engine::Engine() noexcept {
	// clang-format off
	m = new EnginePimpl {
		.thread_pool = ThreadPool::create(),
		.logger = logging::Logger::create()
	};
	// clang-format on

	instance = this;
}

auto Engine::get() noexcept -> Engine* {
	// If at any point toast doesn't exist just crash the damn game
	assert(instance && "Toast Engine doesn't exist");
	return instance;
}

void Engine::tick() { }

auto Engine::shouldClose() -> bool {
	return false;
}

}

// ffi stuff
extern "C" {

auto toast_create() -> engine_t* {
	return reinterpret_cast<engine_t*>(new toast::Engine());
}

void toast_tick() {
	toast::Engine::get()->tick();
}

auto toast_should_close() -> int {
	return toast::Engine::get()->shouldClose();
}

void toast_destroy(engine_t* e) {
	delete reinterpret_cast<toast::Engine*>(e);
}
}
