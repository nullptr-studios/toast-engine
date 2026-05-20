#include "engine.hpp"

#include "application.hpp"
#include "events/event.hpp"
#include "ffi/engine.h"    // ffi
#include "logger.hpp"
#include "renderer/SDLOutputTarget.hpp"
#include "renderer/ShaderCompiler.hpp"
#include "renderer/SharedTextureOutputTarget.hpp"
#include "renderer/VulkanCore.hpp"
#include "renderer/VulkanPipeline.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "thread_pool.hpp"
#include "window/base_window.hpp"
#include "window/sdl_window.hpp"

#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <memory>
#include <span>

namespace toast {

namespace {
IApplication* active_application = nullptr;

auto createTrianglePipeline(const renderer::VulkanCore& core, vk::Format color_format, vk::Extent2D extent)
    -> std::unique_ptr<renderer::VulkanPipeline> {
	// Compile shader
	auto shader_spirv = renderer::ShaderCompiler::compileShader("./triangle.slang");

	// Create pipeline
	renderer::VulkanPipeline::Config config {
	  .debug_name = "Triangle Pipeline",
	  .color_format = color_format,
	  .extent = extent,
	  .shader_spirv = shader_spirv,
	};

	return std::make_unique<renderer::VulkanPipeline>(core, config);
}

}

Engine* Engine::instance = nullptr;

struct EnginePimpl {
	std::unique_ptr<ThreadPool> thread_pool;
	std::unique_ptr<logging::Logger> logger;
	std::unique_ptr<IBaseWindow> window;
	std::unique_ptr<renderer::VulkanCore> vulkan_core;
	std::unique_ptr<renderer::VulkanRenderer> renderer;
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
	// Poll window events
#if DEBUG
	if (m->window) {
		m->window->pollEvents();
	}
#else
	m->window->pollEvents();
#endif

	event::pollEvents();

	// Run application logic
	if (active_application) {
		active_application->tick();
	}

	if (m->renderer) {
		m->renderer->drawFrame();
	}
}

auto Engine::shouldClose() -> bool {
	return m->window ? m->window->shouldClose() : false;
}

void Engine::createSDLWindow(const char* w_name) {
	m->window = std::make_unique<SDLWindow>(w_name, 1080, 720, static_cast<int>(SDL_WINDOW_VULKAN));

	auto* sdl_window = static_cast<SDL_Window*>(m->window->nativeHandle());
	auto instance_extensions = renderer::SDLOutputTarget::getRequiredInstanceExtensions(sdl_window);
	auto device_extensions = renderer::SDLOutputTarget::getRequiredDeviceExtensions();
	m->vulkan_core = std::make_unique<renderer::VulkanCore>(instance_extensions, device_extensions);

	auto output_target = std::make_unique<renderer::SDLOutputTarget>(
	    *m->vulkan_core, sdl_window, renderer::SDLOutputTarget::queryExtent(sdl_window)
	);
	auto extent = output_target->getExtent();
	auto color_format = output_target->getColorFormat();

	auto pipeline = createTrianglePipeline(*m->vulkan_core, color_format, extent);

	m->renderer = std::make_unique<renderer::VulkanRenderer>(*m->vulkan_core, std::move(output_target), std::move(pipeline));
}

void Engine::createAvaloniaWindow() {
	m->vulkan_core = std::make_unique<renderer::VulkanCore>(std::span<const char* const> {}, std::span<const char* const> {});

	auto output_target = std::make_unique<renderer::SharedTextureOutputTarget>(*m->vulkan_core, vk::Extent2D(1080, 720));
	auto color_format = output_target->getColorFormat();
	auto extent = output_target->getExtent();

	auto pipeline = createTrianglePipeline(*m->vulkan_core, color_format, extent);

	m->renderer = std::make_unique<renderer::VulkanRenderer>(*m->vulkan_core, std::move(output_target), std::move(pipeline));
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
