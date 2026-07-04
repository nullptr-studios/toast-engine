#include "engine.hpp"

#include "application.hpp"
#include "assets/asset_manager.hpp"
#include "assets/prefab.hpp"
#include "audio/audio_system.hpp"
#include "events/event.hpp"
#include "events/listener.hpp"
#include "ffi/engine.h"    // ffi
#include "input/haptics_system.hpp"
#include "input/input_events.hpp"
#include "input/input_system.hpp"
#include "logger.hpp"
#include "renderer/sdl_output_target.hpp"
#include "renderer/shader_compiler.hpp"
#include "renderer/shared_texture_output_target.hpp"
#include "renderer/vulkan_core.hpp"
#include "renderer/vulkan_pipeline.hpp"
#include "renderer/vulkan_renderer.hpp"
#include "thread_pool.hpp"
#include "time.hpp"
#include "window/base_window.hpp"
#include "window/sdl_window.hpp"
#include "window/window_events.hpp"
#include "world/reflect.hpp"
#include "world/workspace.hpp"
#include "world/workspace_events.hpp"
#include "world/world.hpp"

#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>

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
	std::unique_ptr<ThreadPool> thread_pool = nullptr;
	std::unique_ptr<logging::Logger> logger = nullptr;
	std::unique_ptr<IBaseWindow> window = nullptr;
	std::unique_ptr<World> world = nullptr;
	std::unique_ptr<assets::AssetManager> asset_manager = nullptr;
	std::unique_ptr<input::InputSystem> input_system = nullptr;
	std::unique_ptr<input::HapticsSystem> haptics_system = nullptr;
	std::unique_ptr<renderer::VulkanCore> vulkan_core = nullptr;
	std::unique_ptr<renderer::VulkanRenderer> renderer = nullptr;
	std::unique_ptr<audio::AudioSystem> audio_system = nullptr;
	Time time;
	event::Listener listener;
	toast::NodeRegistry reflection_registry;

	// owned by renderer's output target
	renderer::SharedTextureOutputTarget* shared_target = nullptr;

	std::mutex owners_mutex;
	std::map<toast::UID, std::unique_ptr<INodeOwner>> owners;
	toast::UID active_workspace {0};
};

Engine::~Engine() noexcept {
	delete m;
	instance = nullptr;
}

Engine::Engine() noexcept {
	instance = this;

	// clang-format off
	m = new EnginePimpl {
		.thread_pool = ThreadPool::create(),
		.logger = logging::Logger::create()
	};
	// clang-format on

	/*
	 *	IMPORTANT:
	 *	If you are planning on initializing something here you should
	 *	consider doing it on Engine::init() instead
	 *
	 *	This code runs before we even set our working directory and create our
	 *	game project, so there's not a lot of reason something outside the logger
	 *	and the thread pool (logger depends on it) to be here
	 *
	 *	Be smart like toast and initialize things on the init() function
	 *	- xein <3
	 */
}

auto Engine::get() noexcept -> Engine* {
	// If at any point toast doesn't exist just crash the damn game
	assert(instance && "Toast Engine doesn't exist");
	return instance;
}

void Engine::init() {
	TracySetProgramName("ToastEngine");
#ifdef TRACY_ENABLE
	tracy::SetThreadName("Main Thread");
#endif

	event::registerProtoEvents();
	registerEngineTypes();

	m->asset_manager = std::make_unique<assets::AssetManager>();

	m->input_system = std::make_unique<input::InputSystem>();
	m->haptics_system = std::make_unique<input::HapticsSystem>();

	// TODO: This should be moved into VulkanRenderer
	m->listener.subscribe<event::WindowResize>([this](const event::WindowResize& e) {
		if (!m->renderer || !m->vulkan_core || e.width <= 0 || e.height <= 0) {
			return false;
		}

		m->renderer->resize(vk::Extent2D {static_cast<uint32_t>(e.width), static_cast<uint32_t>(e.height)});
		return false;
	});

	m->listener.subscribe<event::WorkspaceDestroy>([this](const event::WorkspaceDestroy& e) {
		destroyWorkspace(e.handle);
		return false;
	});

	// The editor tells us which workspace is focused; only that one answers hierarchy requests
	m->listener.subscribe<event::SetActiveWorkspace>([this](const event::SetActiveWorkspace& e) {
		m->active_workspace = e.handle;
		event::send<event::RequestHierarchyUpdate>();
		return false;
	});

	m->audio_system = std::make_unique<audio::AudioSystem>();
}

void Engine::tick() {
	ZoneScoped;

	m->time.tick();

	// Poll window events
#ifndef NDEBUG
	if (m->window) {
		m->window->pollEvents();
	}
#else
	m->window->pollEvents();
#endif

	event::pollEvents();

	m->input_system->tick();
	m->haptics_system->tick();

	{
		std::scoped_lock lock(m->owners_mutex);
		ZoneScopedN("NodeOwners::tick()");
		for (const auto& [_, node_owner] : m->owners) {
			node_owner->tick();
		}
	}

	// Run application layer
	if (active_application) {
		ZoneScopedN("GameLayer::tick()");
		active_application->tick();
	}

	if (m->audio_system) {
		m->audio_system->tick();
	}

	if (m->renderer) {
		m->renderer->drawFrame();
	}

	// TODO: HACK: we should introduce a proper relax mode
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
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

auto Engine::createWorkspace(std::string_view type) -> std::pair<UID, std::string> {
	UID uid;
	uid.generate();
	std::scoped_lock lock(m->owners_mutex);
	auto [it, _] = m->owners.emplace(uid, std::make_unique<Workspace>(type, uid));
	std::string name = it->second->name();
	return {uid, name};
}

auto Engine::openWorkspace(UID uid) -> std::pair<UID, std::string> {
	std::scoped_lock lock(m->owners_mutex);
	if (m->owners.contains(uid)) {
		TOAST_ERROR("Engine", "Trying to open workspace {} which is already open", uid);
		return {};
	}

	auto [it, _] = m->owners.emplace(uid, std::make_unique<Workspace>(uid));

	auto* ws = static_cast<Workspace*>(it->second.get());
	if (!ws->isValid()) {
		TOAST_ERROR("Engine", "Failed to load asset {} into workspace", uid);
		m->owners.erase(it);
		return {};
	}

	std::string name = it->second->name();
	return {uid, name};
}

void Engine::destroyWorkspace(UID handle) {
	std::scoped_lock lock(m->owners_mutex);
	m->owners.erase(handle);
	if (m->active_workspace.data() == handle.data()) {
		m->active_workspace = UID {0};
	}
}

auto Engine::activeWorkspace() -> UID {
	return m->active_workspace;
}

auto Engine::getViewportFrame(void* dst, uint32_t dst_capacity, renderer::ViewportFrameDesc* out) -> int {
	if (!m->shared_target) {
		return 0;
	}
	return m->shared_target->copyLatestFrame(dst, dst_capacity, out);
}

void pushApplicationLayer(IApplication* app) {
	if (active_application && active_application != app) {
		active_application->destroy();
		delete active_application;
	}

	active_application = app;
}

}

// Tracy memory profiling
#ifdef DEBUG
// NOLINTBEGIN(cppcoreguidelines-no-malloc)
auto operator new(std::size_t count) -> void* {
	auto* ptr = malloc(count);
	tracy::Profiler::MemAllocCallstack(ptr, count, TRACY_CALLSTACK, true);
	return ptr;
}

void operator delete(void* ptr) noexcept {
	tracy::Profiler::MemFreeCallstack(ptr, TRACY_CALLSTACK, true);
	free(ptr);
}

// NOLINTEND(cppcoreguidelines-no-malloc)
#endif

// ffi stuff
extern "C" {

auto toast_create() noexcept -> engine_t* {
	return reinterpret_cast<engine_t*>(new toast::Engine());
}

void toast_init() noexcept {
	toast::Engine::get()->init();

	if (toast::active_application) {
		toast::active_application->begin();
	}
}

void toast_create_SDL_window(const char* w_name) noexcept {
	toast::Engine::get()->createSDLWindow(w_name);
}

void toast_create_avalonia_window() noexcept {
	toast::Engine::get()->createAvaloniaWindow();
}

void toast_tick() noexcept {
	toast::Engine::get()->tick();
}

auto toast_should_close() noexcept -> int {
	return toast::Engine::get()->shouldClose();
}

void toast_destroy(engine_t* e) noexcept {
	delete reinterpret_cast<toast::Engine*>(e);
}

void toast_set_working_directory(
    const char* assets, const char* artworks, const char* cache, const char* saved, const char* core
) noexcept {
	assets::AssetManager::setPaths({.assets = assets, .artworks = artworks, .cache = cache, .saved = saved, .core = core});
}

auto toast_viewport_get_frame(void* dst, uint32_t dst_capacity, toast_viewport_frame_t* out) noexcept -> int {
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

auto toast_create_workspace(const char* type) noexcept -> workspace_result {
	auto [uid, name] = toast::Engine::get()->createWorkspace(type);

	static thread_local std::string s_name;
	s_name = std::move(name);
	return {.uid = uid.data(), .name = s_name.c_str()};
}

auto toast_open_workspace(const char* uid) noexcept -> workspace_result {
	auto [root_uid, name] = toast::Engine::get()->openWorkspace(toast::UID::fromString(uid));

	static thread_local std::string s_name;
	s_name = std::move(name);
	return {.uid = root_uid.data(), .name = s_name.c_str()};
}

void toast_rename_prefab_root(const char* path, const char* new_name) noexcept {
	std::ifstream file_in(path, std::ios::binary | std::ios::ate);
	if (!file_in.is_open()) {
		return;
	}

	const auto size = static_cast<std::size_t>(file_in.tellg());
	file_in.seekg(0, std::ios::beg);
	std::vector<uint8_t> bytes(size);
	if (!file_in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
		return;
	}
	file_in.close();

	// Detect binary format by the TNODE magic header; otherwise it's text
	const bool is_binary = bytes.size() >= 6 && bytes[0] == 'T' && bytes[1] == 'N' && bytes[2] == 'O' && bytes[3] == 'D' &&
	                       bytes[4] == 'E' && bytes[5] == '\0';

	std::unique_ptr<assets::Prefab> prefab;
	if (is_binary) {
		const std::span<const uint8_t> byte_span {bytes};
		prefab = std::make_unique<assets::Prefab>(byte_span);
	} else {
		std::string text_content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		std::istringstream ss {text_content};
		prefab = std::make_unique<assets::Prefab>(ss);
	}

	if (!prefab || prefab->nodes.empty()) {
		return;
	}
	prefab->nodes[0].name = new_name;

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (is_binary) {
		auto new_bytes = prefab->toBinary();
		out.write(reinterpret_cast<const char*>(new_bytes.data()), static_cast<std::streamsize>(new_bytes.size()));
	} else {
		const auto text = prefab->toFile();
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
	}
}

void toast_create_tnode(const char* path, const char* node_type) noexcept {
	const auto stem = std::filesystem::path(path).stem().string();

	// temp workspace
	toast::Workspace temp_ws(node_type, toast::UID(static_cast<uint64_t>(-1ULL)));

	assets::Prefab prefab(temp_ws.rootNode());
	if (!prefab.nodes.empty()) {
		prefab.nodes[0].name = stem;
	}

	const auto bytes = prefab.serialize(assets::SaveMode::editor);
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void toast_reload_manifest() noexcept {
	auto& mgr = assets::AssetManager::get();
	mgr.clearUnusedAssets();
	mgr.reloadManifest();
}

void toast_haptics_test(const char* toml_text) noexcept {
	if (toml_text == nullptr) {
		return;
	}
	try {
		toml::table table = toml::parse(std::string_view {toml_text});
		auto* haptic = new assets::Haptic(table);
		assets::AssetHandle<assets::Haptic> handle {haptic, toast::UID::make(), "editor://haptic_test"};
		event::send<event::PlayHapticDirect>(uint32_t {0}, std::move(handle));
	} catch (const std::exception& e) { TOAST_ERROR("Haptics", "Failed to parse test haptic: {}", e.what()); }
}
}
