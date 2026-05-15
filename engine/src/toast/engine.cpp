#include "engine.hpp"

#include "application.hpp"
#include "events/event.hpp"
#include "ffi/engine.h"    // ffi
#include "logger.hpp"
#include "thread_pool.hpp"
#include "window/base_window.hpp"
#include "window/sdl_window.hpp"

#include <cassert>
#include <memory>
#include <tracy/Tracy.hpp>

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

	TracySetProgramName("ToastEngine");
	tracy::SetThreadName("Main Thread");

	instance = this;
}

auto Engine::get() noexcept -> Engine* {
	// If at any point toast doesn't exist just crash the damn game
	assert(instance && "Toast Engine doesn't exist");
	return instance;
}

void Engine::tick() {
	FrameMark;
	ZoneScoped;

	// Poll window events
#if DEBUG
	if (m->window) {
		m->window->pollEvents();
	}
#else
	m->window->pollEvents();
#endif

	event::pollEvents();

	// dummy stress test
	std::this_thread::sleep_for(std::chrono::milliseconds(1));

	// Run application logic
	if (active_application) {
		ZoneScopedN("IApplication::tick()");
		active_application->tick();
	}
}

auto Engine::shouldClose() -> bool {
	return false;
}

void Engine::createSDLWindow(const char* w_name) {
	m->window = std::make_unique<SDLWindow>(w_name);
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

// Tracy memory profiling
#ifdef DEBUG
void* operator new(std::size_t count) {
	auto ptr = malloc(count);
	TracyAlloc(ptr, count);
	return ptr;
}

void operator delete(void* ptr) noexcept {
	TracyFree(ptr);
	free(ptr);
}
#endif

// ffi stuff
extern "C" {

auto toast_create() -> engine_t* {
	return reinterpret_cast<engine_t*>(new toast::Engine());
}

void toast_create_SDL_window(const char* w_name) {
	toast::Engine::get()->createSDLWindow(w_name);
}

void toast_create_avalonia_window() {
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
