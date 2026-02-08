#include "engine.h"
#include "engine.hpp"

#include <cassert>
#include <print>

namespace toast {

Engine* Engine::instance = nullptr;

struct EnginePimpl {

};

Engine::Engine() noexcept {
	m = new EnginePimpl{};
	instance = this;
}

Engine::~Engine() noexcept = default;

Engine* Engine::get() noexcept {
	// If at any point toast doesn't exist just crash the damn game -x
	assert(instance && "Toast Engine doesn't exist");
	return instance;
}

void Engine::tick() {
	test();
}

bool Engine::shouldClose() {
	return false;
}

void Engine::test() {
	std::println("Test!!!");
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
