#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <mmsystem.h>
// clang-format on
#pragma comment(lib, "winmm.lib")
#endif

#include "Toast/Engine.hpp"

#include "Audio/AudioSystem.hpp"
#include "Event/EventSystem.hpp"
#include "ForceLink.cpp"
#include "Input/InputSystem.hpp"
#include "Physics/PhysicsSystem.hpp"
#include "Toast/CoroutineHandler.hpp"
#include "Toast/Factory.hpp"
#include "Toast/Log.hpp"
#include "Toast/Memory.hpp"
#include "Toast/Objects/Scene.hpp"
#include "Toast/ProjectSettings.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/HUD/HUDLayer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/LayerStack.hpp"
#include "Toast/Renderer/OpenGL/OpenGLRenderer.hpp"
#include "Toast/Time.hpp"
#include "Toast/Ui/Manager.hpp"
#include "Toast/Window/Window.hpp"
#include "Toast/World.hpp"

#include <chrono>
#include <memory>
#include <thread>

#ifdef _WIN32
#include <intrin.h>
#endif

#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace toast {

#ifdef _WIN32
static HANDLE g_singleInstanceMutex = nullptr;

static bool AcquireSingleInstanceLock(const std::string& mutexName) {
	g_singleInstanceMutex = CreateMutexA(nullptr, TRUE, mutexName.c_str());
	if (!g_singleInstanceMutex) {
		return false;
	}
	
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(g_singleInstanceMutex);
		g_singleInstanceMutex = nullptr;
		return false;
	}
	return true;
}

static void ReleaseSingleInstanceLock() {
	if (g_singleInstanceMutex) {
		CloseHandle(g_singleInstanceMutex);
		g_singleInstanceMutex = nullptr;
	}
}
#endif

Engine* Engine::m_instance;
double Engine::purge_timer = 0.0;

double updateTimer = 0.0;

struct Engine::Pimpl {
	std::unique_ptr<Time> time;
	std::unique_ptr<event::EventSystem> eventSystem;
	std::unique_ptr<Window> window;
	std::unique_ptr<input::InputSystem> inputSystem;
	std::unique_ptr<World> gameWorld;
	std::unique_ptr<renderer::IRendererBase> renderer;
	std::unique_ptr<renderer::LayerStack> layerStack;
	std::unique_ptr<Factory> factory;
	std::unique_ptr<resource::ResourceManager> resourceManager;
	std::unique_ptr<ProjectSettings> projectSettings;
	std::unique_ptr<physics::PhysicsSystem> physicsSystem;
	std::unique_ptr<ui::UiSystem> uiSystem;
	audio::AudioSystem* audioSystem;
	std::unique_ptr<CoroutineHandler> coroutineHandler;
};

void Engine::Run(int argc, char** argv) {
	// Before starting the engine we store the arguments 0x
	// OPTIMIZE: Maybe we can put this on the entrypoint to optimize winmain
	m_arguments.clear();
	m_arguments.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0);
	for (int i = 1; i < argc; ++i) {
		m_arguments.emplace_back(argv[i]);
	}

	// Check for single-instance flag
	bool singleInstanceMode = false;
	for (const auto& arg : m_arguments) {
		if (arg == "-single-instance") {
			singleInstanceMode = true;
			break;
		}
	}

	// Enforce single instance if requested
#ifdef _WIN32
	if (singleInstanceMode) {
		if (!AcquireSingleInstanceLock("Toast_Engine_Single_Instance")) {
			TOAST_ERROR("Another instance of the game is already running. Exiting.");
			MessageBoxA(nullptr, "Another instance of the game is already running.", "Game Already Running", MB_OK | MB_ICONWARNING);
			return;
		}
	}
#endif

	Init();
	purge_timer = 0.0f;

	auto* window = m->window.get();
	auto* world = m->gameWorld.get();

	updateTimer += Time::delta();

#ifdef _WIN32
	// Request 1ms timer resolution for accurate sleep_for on Windows.
	// Without this, Sleep/sleep_for is ~15ms, making any FPS cap below 70 wildly inaccurate.
	timeBeginPeriod(1);
#endif

	using clock_t = std::chrono::high_resolution_clock;
	clock_t::time_point frameStart;

	while (!GetShouldClose()) {
		updateTimer = 0.0;
		// This is our frame 0x
		PROFILE_ZONE_N("Frame");

		// Record frame start at the very top so the limiter measures the full frame cost
		frameStart = clock_t::now();

		// avoid heavy work if minimized
		if (window->IsMinimized()) {
			// Still poll events
			window->PollEventsOnly();
			m_windowShouldClose.store(window->ShouldClose(), std::memory_order_relaxed);
			// Back off a bit to avoid busy-waiting while minimized
			window->WaitEventsTimeout(0.016);    // ~60 FPS
			PROFILE_FRAME;
			continue;
		}

		// Poll OS events early in the frame to reduce input latency
		window->PollEventsOnly();

		m->time->Tick();

		m->resourceManager->LoadResourcesMainThread();

		m->eventSystem->PollEvents();

		// Ensure any pending Begin calls are executed as early as possible in the frame
		world->RunBeginQueue();

		m->coroutineHandler->Tick();
		m->inputSystem->Tick();

		world->EarlyTick();

		// Interpolate rb transforms before rendering
		physics::PhysicsSystem::UpdateVisualInterpolation();

		world->Tick();
		world->LateTick();

		physics::PhysicsSystem::MainThreadLateTick();

#ifdef TOAST_EDITOR
		world->EditorTick();
#endif

		m->layerStack->TickLayers();

		Render();

		// Start the ImGui frame, only for editor
#ifdef TOAST_EDITOR
		m->renderer->StartImGuiFrame();
		EditorTick();
		m->renderer->EndImGuiFrame();
#endif

		m->audioSystem->Tick();

		// Swap after all rendering and UI is done
		window->SwapBuffers();

		// Only when VSync is OFF; when VSync is on the driver already paces us.
		const auto& cfg = m->renderer->GetRendererConfig();
		if (!cfg.vSync && cfg.maxFPS > 0) {
			const auto targetDuration = std::chrono::duration<double>(1.0 / static_cast<double>(cfg.maxFPS));
			const auto targetTime = frameStart + targetDuration;

			auto now = clock_t::now();
			while (targetTime - now > std::chrono::milliseconds(2)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				now = clock_t::now();
			}
			while (clock_t::now() < targetTime) {
#ifdef _WIN32
				_mm_pause();
#endif
			}
		}

		// DestroyQueue also removes scenes 0x
		world->RunDestroyQueue();

		m_windowShouldClose.store(window->ShouldClose(), std::memory_order_relaxed);

		// Purge unused resources from the cache (every 120 seconds)
		const double current_uptime = Time::uptime();
		if (current_uptime - purge_timer >= 120.0) {
			purge_timer = current_uptime;
			TOAST_TRACE("Purging unused resources...");
			resource::PurgeResources();
		}

		PROFILE_FRAME;
	}
	Close();
#ifdef _WIN32
	ReleaseSingleInstanceLock();
#endif
}

bool Engine::GetShouldClose() const {
	return m_windowShouldClose.load(std::memory_order_relaxed);
}

Engine* Engine::get() {
	return m_instance;
}

Engine::Engine() {
	if (m_instance) {
		throw ToastException("There is already an instance of Engine");
	}
	m_instance = this;
	m = new Pimpl;
}

void Engine::Init() {
	PROFILE_ZONE_N("Engine Init");

	// Starting logging system 0x
	Log::Init();
	TOAST_INFO("Initializing Toast Engine...");
	if (!m_arguments.empty()) {
		TOAST_TRACE("Called with {0} arguments", m_arguments.size());
	}

	m->resourceManager = std::make_unique<resource::ResourceManager>(false);
	// Starting time tracking
	m->time = std::make_unique<Time>();

	// Starting event system
	m->eventSystem = std::make_unique<event::EventSystem>();

	m->projectSettings = std::make_unique<ProjectSettings>();

	// Create window
	m->window = std::make_unique<Window>(1280, 720, "Paw on the trigger");

	// LayerStack must be created BEFORE renderer, as renderer gets the instance during init
	m->layerStack = std::make_unique<renderer::LayerStack>();

	m->renderer = std::make_unique<renderer::OpenGLRenderer>();

	// Create input system
	m->inputSystem = std::make_unique<input::InputSystem>();

	// Create the Game World
	m->gameWorld = std::make_unique<World>();

	// Create the Factory
	m->factory = std::make_unique<Factory>();

	// Imguilayer testing purposes
	m->layerStack->PushOverlay(new renderer::DebugDrawLayer());

	// Physics System
	m->physicsSystem = std::make_unique<physics::PhysicsSystem>();

	// Audio
	auto audio_result = audio::AudioSystem::create();
	TOAST_ASSERT(audio_result, "Failed to initialize Audio System");
	m->audioSystem = audio_result.value();

	// Ui
	m->uiSystem = std::make_unique<ui::UiSystem>(*m->window, false);
	// auto* hud = new renderer::HUD::HUDLayer(m->window->GetWindow(), 1920, 1080, false); // TODO: you can delete this later
	// m->layerStack->PushOverlay(hud);
	// hud->LoadURL("file:///assets/UI/hud.html");

	m->coroutineHandler = std::make_unique<CoroutineHandler>();

	Begin();

#ifndef TOAST_EDITOR
	physics::PhysicsSystem::start();
#endif
}

#ifdef TOAST_EDITOR
void Engine::EditorTick() { }
#endif

void Engine::Render() {
	PROFILE_ZONE_C(0xFF0000);    // Red for rendering
	m->renderer->Render();
}

void Engine::Close() {
#ifdef _WIN32
	timeEndPeriod(1);
#endif
#ifdef TRACY_ENABLE
	DisableTracyProfiling();
#endif
	delete m;
}

void Engine::ForcePurgeResources() {
	// Force purge by setting timer to a very old value
	purge_timer = -200.0;
}

}
