#include "engine.hpp"

#include "application.hpp"
#include "events/event.hpp"
#include "events/listener.hpp"
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
#include "window/window_events.hpp"

#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace toast {

namespace {
IApplication* active_application = nullptr;

// FIXME: DEBUGGING PURPORSES
auto createTrianglePipeline(
    const renderer::VulkanCore& core, vk::Format color_format, vk::Extent2D extent, std::optional<vk::Format> depth_format
) -> std::unique_ptr<renderer::VulkanPipeline> {
	// Compile shader
	auto shader_spirv = renderer::ShaderCompiler::compileShader("./mirrors.slang");

	// Define our runtime layout payload requirements
	std::vector<vk::DescriptorSetLayoutBinding> descriptor_bindings;
	descriptor_bindings.emplace_back(
	    0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
	);

	// Create pipeline using the new generic configuration parameters
	renderer::VulkanPipeline::Config config {
	  .debug_name = "triangle Pipeline",
	  .color_format = color_format,
	  .extent = extent,
	  .shader_spirv = shader_spirv,
	  .depth_format = depth_format,
	  .descriptor_bindings = descriptor_bindings,
	  .push_constant_ranges = {},
	  .cull_mode = vk::CullModeFlagBits::eBack
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
	event::Listener resize_listener;

	// owned by renderer's output target
	renderer::SharedTextureOutputTarget* shared_target = nullptr;
};

Engine::Engine() noexcept {
	// clang-format off
	m = new EnginePimpl {
		.thread_pool = ThreadPool::create(),
		.logger = logging::Logger::create()
	};
	// clang-format on

	// TODO: This should be moved into VulkanRenderer
	m->resize_listener.subscribe<event::WindowResize>([this](const event::WindowResize& e) {
		if (!m->renderer || !m->vulkan_core || e.width <= 0 || e.height <= 0) {
			return false;
		}

		m->renderer->resize(vk::Extent2D {static_cast<uint32_t>(e.width), static_cast<uint32_t>(e.height)});
		return false;
	});

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

	std::this_thread::sleep_for(std::chrono::milliseconds(16));
}

auto Engine::shouldClose() -> bool {
	return m->window ? m->window->shouldClose() : false;
}

void Engine::createSDLWindow(const char* w_name) {
	// create window
	m->window = std::make_unique<SDLWindow>(w_name, 1080, 720, SDL_WINDOW_VULKAN);

	// get window handle
	auto* sdl_window = static_cast<SDL_Window*>(m->window->nativeHandle());
	auto instance_extensions = renderer::SDLOutputTarget::getRequiredInstanceExtensions(sdl_window);
	auto device_extensions = renderer::SDLOutputTarget::getRequiredDeviceExtensions();
	// create vulkan core
	m->vulkan_core = std::make_unique<renderer::VulkanCore>(instance_extensions, device_extensions);

	// create output texture
	auto output_target = std::make_unique<renderer::SDLOutputTarget>(
	    *m->vulkan_core, sdl_window, renderer::SDLOutputTarget::queryExtent(sdl_window)
	);
	auto extent = output_target->getExtent();
	auto color_format = output_target->getColorFormat();
	auto depth_format = renderer::VulkanRenderer::selectDepthFormat(*m->vulkan_core);

	// create debug pipeline
	auto pipeline = createTrianglePipeline(*m->vulkan_core, color_format, extent, depth_format);

	// create renderer
	m->renderer = std::make_unique<renderer::VulkanRenderer>(*m->vulkan_core, std::move(output_target), std::move(pipeline));
}

void Engine::createAvaloniaWindow() {
	m->vulkan_core = std::make_unique<renderer::VulkanCore>(std::span<const char* const> {}, std::span<const char* const> {});

	auto output_target = std::make_unique<renderer::SharedTextureOutputTarget>(*m->vulkan_core, vk::Extent2D(1080, 720));
	auto color_format = output_target->getColorFormat();
	auto extent = output_target->getExtent();
	auto depth_format = renderer::VulkanRenderer::selectDepthFormat(*m->vulkan_core);

	m->shared_target = output_target.get();

	auto pipeline = createTrianglePipeline(*m->vulkan_core, color_format, extent, depth_format);

	m->renderer = std::make_unique<renderer::VulkanRenderer>(*m->vulkan_core, std::move(output_target), std::move(pipeline));
}

int Engine::getViewportFrame(void* dst, uint32_t dstCapacity, renderer::ViewportFrameDesc* out) {
	if (!m->shared_target) {
		return 0;
	}
	return m->shared_target->copyLatestFrame(dst, dstCapacity, out);
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

int toast_viewport_get_frame(void* dst, uint32_t dst_capacity, toast_viewport_frame_t* out) {
	toast::renderer::ViewportFrameDesc desc {};
	const int result = toast::Engine::get()->getViewportFrame(dst, dst_capacity, &desc);
	if (out) {
		out->width = desc.width;
		out->height = desc.height;
		out->row_pitch = desc.row_pitch;
		out->frame_id = desc.frame_id;
	}
	return result;
}

void toast_send_mouse_position(float x, float y) {
	event::send<event::WindowMousePosition>(x, y);
}

void toast_send_mouse_button(int button, int action, int mods) {
	event::send<event::WindowMouseButton>(button, action, mods);
}

void toast_send_mouse_scroll(float x, float y) {
	event::send<event::WindowMouseScroll>(x, y);
}

void toast_send_key(int key, int scancode, int action, int mods) {
	event::send<event::WindowKey>(key, scancode, action, mods);
}

void toast_send_char(unsigned codepoint) {
	event::send<event::WindowChar>(codepoint);
}

void toast_send_resize(int width, int height) {
	event::send<event::WindowResize>(width, height);
}
}
