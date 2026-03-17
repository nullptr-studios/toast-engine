#include "engine.h"
#include "engine.hpp"

#include "thread_pool.hpp"
#include "logger.hpp"

#include <cassert>
#include <chrono>
#include <memory>
#include <print>
#include <thread>

namespace toast {

Engine* Engine::instance = nullptr;

struct EnginePimpl {
	std::unique_ptr<ThreadPool> thread_pool;
	std::unique_ptr<Logger> logger;
};

Engine::Engine() noexcept {
	m = new EnginePimpl{
		.thread_pool = ThreadPool::create(),
		.logger = Logger::create()
	};

	instance = this;
}

Engine::~Engine() noexcept { };

Engine* Engine::get() noexcept {
	// If at any point toast doesn't exist just crash the damn game -x
	assert(instance && "Toast Engine doesn't exist");
	return instance;
}

void Engine::tick() {
	Logger::log("", 0, 0, "", "Test message");

	using namespace std::chrono_literals;
	std::this_thread::sleep_for(10s);
}

bool Engine::shouldClose() {
	return false;
}

}

// ffi stuff
extern "C" {

engine_t* toast_create() {
	std::println("Creating Toast Engine!!");
	return reinterpret_cast<engine_t*>(new toast::Engine());
}

void toast_tick() {
	toast::Engine::get()->tick();
}

int toast_should_close() {
	return toast::Engine::get()->shouldClose();
}

void toast_destroy(engine_t* e) {
	std::println("Deleting Toast Engine!!");
	delete reinterpret_cast<toast::Engine*>(e);
}

}
