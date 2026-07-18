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
#include "project_settings.hpp"
#include "reflect/reflect.hpp"
#include "renderer/passes/debug_pass.hpp"
#include "renderer/passes/mesh_pass.hpp"
#include "renderer/sdl_output_target.hpp"
#include "renderer/shader_compiler.hpp"
#include "renderer/shader_layout.hpp"
#include "renderer/shared_texture_output_target.hpp"
#include "renderer/vulkan_core.hpp"
#include "renderer/vulkan_renderer.hpp"
#include "scripting/lua_state.hpp"
#include "thread_pool.hpp"
#include "time.hpp"
#include "ui/render/ui_pass.hpp"
#include "ui/ui_system.hpp"
#include "window/base_window.hpp"
#include "window/sdl_window.hpp"
#include "window/window_events.hpp"
#include "world/camera.hpp"
#include "world/play_workspace.hpp"
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
Camera* camera = nullptr;
float total_time = 0.0;
double clear_assets_timer = 0.0;
double lua_memory_plot_timer = 0.0;
double script_reload_timer = 0.0;

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
	std::unique_ptr<ui::UISystem> ui_system = nullptr;
	std::unique_ptr<ProjectSettings> settings = nullptr;
	std::unique_ptr<scripting::LuaState> lua_state = nullptr;
	Time time;
	event::Listener listener;
	toast::NodeRegistry reflection_registry;

	// owned by renderer's output target
	renderer::SharedTextureOutputTarget* shared_target = nullptr;

	std::mutex owners_mutex;
	std::map<toast::UID, std::unique_ptr<INodeOwner>> owners;
	toast::UID active_workspace {0};
};

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

	// Find the first .toast project file in the project root and load settings
	{
		std::filesystem::path toast_path;
		const auto& proj_root = assets::AssetManager::projectRoot();
		if (!proj_root.empty() && std::filesystem::is_directory(proj_root)) {
			for (const auto& entry : std::filesystem::directory_iterator(proj_root)) {
				if (entry.path().extension() == ".toast") {
					toast_path = entry.path();
					break;
				}
			}
		}
		m->settings = std::make_unique<ProjectSettings>(toast_path);
	}

	// Register content database VFS roots derived from project settings
	{
		const auto& proj_root = assets::AssetManager::projectRoot();
		for (const auto& db : ProjectSettings::databases()) {
			assets::AssetManager::registerDatabase(db, proj_root / db);
		}
	}

	m->asset_manager = std::make_unique<assets::AssetManager>();

	m->input_system = std::make_unique<input::InputSystem>();
	m->haptics_system = std::make_unique<input::HapticsSystem>();

	m->lua_state = scripting::LuaState::create();

	// TODO: This should be moved into VulkanRenderer
	m->listener.subscribe<event::WindowResize>([this](const event::WindowResize& e) {
		if (!m->renderer || !m->vulkan_core || e.width <= 0 || e.height <= 0) {
			return false;
		}

		m->renderer->applyResize(vk::Extent2D {static_cast<uint32_t>(e.width), static_cast<uint32_t>(e.height)});
		return false;
	});

	m->listener.subscribe<event::WorkspaceDestroy>([this](const event::WorkspaceDestroy& e) {
		destroyWorkspace(e.handle);
		event::send<event::ClearUnusedAssets>();
		return false;
	});

	// The editor tells us which workspace is focused; only that one answers hierarchy requests
	m->listener.subscribe<event::SetActiveWorkspace>([this](const event::SetActiveWorkspace& e) {
		m->active_workspace = e.handle;
		event::send<event::RequestHierarchyUpdate>();
		return false;
	});

	// A script source changed on disk
	// rebuild the runtimes that use it everywhere and let the schedulers recompute
	m->listener.subscribe<event::ScriptAssetReloaded>([this](const event::ScriptAssetReloaded& e) {
		{
			std::scoped_lock lock(m->owners_mutex);
			for (const auto& [_, node_owner] : m->owners) {
				node_owner->reloadScriptsUsing(e.uid);
			}
		}
		World::hotReloadScripts(e.uid);
		event::send<event::RequestHierarchyUpdate>();
		return false;
	});

	m->audio_system = std::make_unique<audio::AudioSystem>();
	m->ui_system = std::make_unique<ui::UISystem>();
}

Engine::~Engine() noexcept {
	if (m) {
		if (m->renderer) {
			m->renderer->stop();
		}

		// remove objects before closing
		{
			std::scoped_lock lock(m->owners_mutex);
			m->owners.clear();
		}
		m->world.reset();
		m->ui_system.reset();
		m->asset_manager.reset();
		m->renderer.reset();
		m->vulkan_core.reset();

		delete m;
		m = nullptr;
	}

	instance = nullptr;
}

void Engine::reloadSettings() {
	// Find the .toast project file in the project root
	std::filesystem::path toast_path;
	const auto& proj_root = assets::AssetManager::projectRoot();
	if (!proj_root.empty() && std::filesystem::is_directory(proj_root)) {
		for (const auto& entry : std::filesystem::directory_iterator(proj_root)) {
			if (entry.path().extension() == ".toast") {
				toast_path = entry.path();
				break;
			}
		}
	}

	// Clear old content database routes and reload settings
	assets::AssetManager::clearDatabases();
	m->settings = std::make_unique<ProjectSettings>(toast_path);

	// Re-register content databases from updated settings
	for (const auto& db : ProjectSettings::databases()) {
		assets::AssetManager::registerDatabase(db, proj_root / db);
	}

	if (m->asset_manager) {
		m->asset_manager->reloadManifest();
	}
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
	total_time += Time::delta();
	camera->worldPos(glm::vec3(std::sin(total_time) * 5.0f, std::cos(total_time) * 5.0f, 5));

	if (m->audio_system) {
		m->audio_system->tick();
	}

	if (m->ui_system) {
		m->ui_system->tick();
	}

	// TODO MOVE THIS
	clear_assets_timer += Time::delta();
	if (clear_assets_timer > 30.0) {
		m->asset_manager->clearUnusedAssets();
		clear_assets_timer = 0.0;
	}

	if (m->renderer) {
		m->renderer->tick(total_time);
	}

	lua_memory_plot_timer += Time::delta();
	if (lua_memory_plot_timer > 1.0) {
		lua_memory_plot_timer = 0.0;
		if (m->lua_state) {
			m->lua_state->plotMemory();
		}
	}

#ifdef DEBUG
	// dev builds hot-reload script sources edited on disk
	script_reload_timer += Time::delta();
	if (script_reload_timer > 1.0) {
		script_reload_timer = 0.0;
		if (m->asset_manager) {
			m->asset_manager->pollModifiedScripts();
		}
	}
#endif
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

	m->renderer = std::make_unique<renderer::VulkanRenderer>(*m->vulkan_core, std::move(output_target));

	// FIXME: change this
	camera = new Camera();
	camera->worldPos(glm::vec3(0));

	m->renderer->setActiveCamera(camera);

	// create debug pipeline
	auto pass = std::make_unique<renderer::MeshPass>(*m->vulkan_core, color_format, depth_format, extent);

	// create renderer

	m->renderer->addRenderPass(std::move(pass));

	// m->renderer->addRenderPass(std::make_unique<renderer::DebugPass>(*m->vulkan_core, color_format, depth_format, extent));

	// UI composites over everything else
	m->renderer->addRenderPass(std::make_unique<ui::UIPass>(*m->vulkan_core, color_format, depth_format, extent));
	if (m->ui_system) {
		m->ui_system->initializeRenderer(*m->vulkan_core);
		m->renderer->setUIFrameBuilder([ui = m->ui_system.get()](renderer::VulkanRenderer::RenderFrame& frame) {
			ui->buildDrawFrame(frame);
		});
	}

	// capped to 240 for now
	m->renderer->setFrameRateLimit(240.0);

	m->renderer->start();
}

void Engine::createAvaloniaWindow() {
	m->vulkan_core = std::make_unique<renderer::VulkanCore>(std::span<const char* const> {}, std::span<const char* const> {});

	auto output_target = std::make_unique<renderer::SharedTextureOutputTarget>(*m->vulkan_core, vk::Extent2D(1080, 720));
	auto color_format = output_target->getColorFormat();
	auto extent = output_target->getExtent();
	auto depth_format = renderer::VulkanRenderer::selectDepthFormat(*m->vulkan_core);

	m->shared_target = output_target.get();

	m->renderer = std::make_unique<renderer::VulkanRenderer>(*m->vulkan_core, std::move(output_target));

	// FIXME: change this
	camera = new Camera();
	camera->worldPos(glm::vec3(0));

	m->renderer->setActiveCamera(camera);

	// create debug pipeline
	auto pass = std::make_unique<renderer::MeshPass>(*m->vulkan_core, color_format, depth_format, extent);

	// create renderer

	m->renderer->addRenderPass(std::move(pass));

	// Editor viewport gets the ground grid / debug lines / gizmo overlay
	m->renderer->addRenderPass(std::make_unique<renderer::DebugPass>(*m->vulkan_core, color_format, depth_format, extent));

	// In-game UI composites over everything else
	m->renderer->addRenderPass(std::make_unique<ui::UIPass>(*m->vulkan_core, color_format, depth_format, extent));
	if (m->ui_system) {
		m->ui_system->initializeRenderer(*m->vulkan_core);
		m->renderer->setUIFrameBuilder([ui = m->ui_system.get()](renderer::VulkanRenderer::RenderFrame& frame) {
			ui->buildDrawFrame(frame);
		});
	}

	m->renderer->setFrameRateLimit(240.0);

	m->renderer->start();
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

auto Engine::openWorkspace(UID uid, std::string_view source_uri) -> std::pair<UID, std::string> {
	std::scoped_lock lock(m->owners_mutex);
	if (m->owners.contains(uid)) {
		TOAST_ERROR("Engine", "Trying to open workspace {} which is already open", uid);
		return {};
	}

	auto [it, _] = m->owners.emplace(uid, std::make_unique<Workspace>(uid, source_uri));

	auto* ws = static_cast<Workspace*>(it->second.get());
	if (!ws->isValid()) {
		TOAST_ERROR("Engine", "Failed to load {} into workspace {}", source_uri, uid);
		m->owners.erase(it);
		return {};
	}

	std::string name = it->second->name();
	return {uid, name};
}

auto Engine::playWorkspace(UID source_handle) -> std::pair<UID, std::string> {
	std::scoped_lock lock(m->owners_mutex);
	auto source_it = m->owners.find(source_handle);
	if (source_it == m->owners.end()) {
		TOAST_ERROR("Engine", "Trying to play workspace {} which doesn't exist", source_handle);
		return {};
	}

	auto* source = static_cast<Workspace*>(source_it->second.get());
	if (!source->isValid()) {
		TOAST_ERROR("Engine", "Trying to play invalid workspace {}", source_handle);
		return {};
	}

	// clone the live tree in memory, the play workspace instantiates from it with the same node UIDs
	assets::Prefab prefab(source->rootNode());

	UID handle;
	handle.generate();
	auto [it, _] = m->owners.emplace(handle, std::make_unique<PlayWorkspace>(handle, prefab));

	auto* play = static_cast<PlayWorkspace*>(it->second.get());
	if (!play->isValid()) {
		TOAST_ERROR("Engine", "Failed to clone workspace {} for play mode", source_handle);
		m->owners.erase(it);
		return {};
	}

	std::string name = it->second->name();
	return {handle, name};
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

void Engine::refreshNodeInfos() {
	std::scoped_lock lock(m->owners_mutex);
	for (const auto& [_, node_owner] : m->owners) {
		node_owner->refreshNodeInfos();
	}
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
	app->registerTypes();
	toast::World::hotReload();

	if (Engine::get() != nullptr) {
		Engine::get()->refreshNodeInfos();
	}
	if (scripting::LuaState::exists()) {
		scripting::LuaState::get().refreshTypeMarkers();
	}
}

void popApplicationLayer(IApplication* app) {
	if (active_application == app) {
		active_application = nullptr;
	}
}

void Engine::popApplication() {
	if (active_application) {
		active_application->destroy();
		delete active_application;
		active_application = nullptr;
	}
}

void Engine::beginApplication() {
	if (active_application) {
		active_application->begin();
	}
}

void Engine::startGame() {
	{
		std::scoped_lock lock(m->owners_mutex);
		// Use a well-known sentinel UID (-1) so the world can be found/removed if needed
		m->owners.emplace(UID {static_cast<uint64_t>(-1ULL)}, std::make_unique<World>());
	}

	// Resolve the start scene from project settings
	const ProjectSettings* ps = ProjectSettings::get();
	if (!ps) {
		TOAST_WARN("Engine", "startGame: no project settings; skipping start scene");
		return;
	}

	const auto& init_scene_handle = toast::ProjectSettings::gameplaySettings().initScene();
	const auto path = init_scene_handle.path();
	if (path.empty()) {
		TOAST_WARN("Engine", "startGame: no init_scene set in project settings");
		return;
	}

	if (path.size() == 11) {
		const toast::UID uid(toast::UID::fromString(path));
		TOAST_INFO("Engine", "startGame: loading init scene by UID {}", uid);
		World::loadNode(uid, true);
	} else {
		TOAST_INFO("Engine", "startGame: loading init scene by URI '{}'", path);
		World::loadNode(path, true);
	}
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

// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
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
    const char* project, const char* artworks, const char* cache, const char* saved, const char* core
) noexcept {
	assets::AssetManager::setPaths({.project = project, .artworks = artworks, .cache = cache, .saved = saved, .core = core});
}

auto toast_viewport_get_frame(void* dst, uint32_t dst_capacity, toast_viewport_frame_t* out) noexcept -> int {
	renderer::ViewportFrameDesc desc {};
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

auto toast_play_workspace(uint64_t source_handle) noexcept -> workspace_result {
	auto [uid, name] = toast::Engine::get()->playWorkspace(toast::UID(source_handle));

	static thread_local std::string s_name;
	s_name = std::move(name);
	return {.uid = uid.data(), .name = s_name.c_str()};
}

auto toast_open_workspace_from(const char* uid, const char* source_uri) noexcept -> workspace_result {
	auto [root_uid, name] = toast::Engine::get()->openWorkspace(toast::UID::fromString(uid), source_uri);

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

void toast_reload_project_settings() noexcept {
	toast::Engine::get()->reloadSettings();
}

void toast_set_load_mode(int mode) noexcept {
	assets::AssetManager::setLoadMode(mode == 1 ? assets::SaveMode::game : assets::SaveMode::editor);
}

void toast_mount_pack(const char* scheme, const char* pak_path) noexcept {
	if (!scheme || !pak_path) {
		return;
	}
	assets::AssetManager::mountPack(scheme, pak_path);
}

void toast_start_game() noexcept {
	toast::Engine::get()->startGame();
}

void toast_begin_application() noexcept {
	toast::Engine::get()->beginApplication();
}

void toast_pop_application() noexcept {
	toast::Engine::get()->popApplication();
}

void toast_bake_asset(const char* uid_str, const char* out_path) noexcept {
	if (!uid_str || !out_path) {
		return;
	}
	try {
		const toast::UID uid(toast::UID::fromString(uid_str));
		auto* asset = assets::AssetManager::get().load(uid);
		if (!asset) {
			TOAST_ERROR("Engine", "toast_bake_asset: could not load asset {}", uid_str);
			return;
		}
		auto* prefab = dynamic_cast<assets::Prefab*>(asset);
		if (!prefab) {
			TOAST_WARN("Engine", "toast_bake_asset: asset {} is not a Prefab, skipping", uid_str);
			return;
		}
		const auto bytes = prefab->serialize(assets::SaveMode::game);
		std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			TOAST_ERROR("Engine", "toast_bake_asset: cannot open output path {}", out_path);
			return;
		}
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		TOAST_TRACE("Engine", "Baked asset {} → {}", uid_str, out_path);
	} catch (const std::exception& e) { TOAST_ERROR("Engine", "toast_bake_asset: {}", e.what()); }
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
