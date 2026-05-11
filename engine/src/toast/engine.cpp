#include "engine.hpp"

#include "application.hpp"
#include "ffi/engine.h"    // ffi
#include "logger.hpp"
#include "thread_pool.hpp"
#include "window/base_window.hpp"
#include "window/sdl_window.hpp"

#include <cassert>
#include <memory>

namespace toast {

namespace {
IApplication* active_application = nullptr;

}

Engine* Engine::instance = nullptr;

struct EnginePimpl {
	std::unique_ptr<ThreadPool> thread_pool;
	std::unique_ptr<logging::Logger> logger;
	std::unique_ptr<IBaseWindow> window;
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

void Engine::tick() {
	if (active_application) {
		active_application->tick();
	}
}

auto Engine::shouldClose() -> bool {
	return false;
}

void Engine::createSDLWindow(const char* wName) {
	m->window = std::make_unique<SDLWindow>(wName);
}

void Engine::createAvaloniaWindow() {
	// TODO
}

void pushApplicationLayer(IApplication* app) {
	if (active_application && active_application != app) {
		active_application->destroy();
	}

	active_application = app;

	if (active_application) {
		active_application->begin();
	}
}

}

// ffi stuff
extern "C" {

auto toast_create() -> engine_t* {
	return reinterpret_cast<engine_t*>(new toast::Engine());
}

void toast_create_SDL_window(const char* wName) {
	toast::Engine::get()->createSDLWindow(wName);
}

void toast_create_Avalonia_window() {
	toast::Engine::get()->createAvaloniaWindow();
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
