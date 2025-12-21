#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "Engine/Renderer/OpenGL/OpenGLRenderer.hpp"

#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include <Engine/Core/Log.hpp>
#include <Engine/Toast/Engine.hpp>
#include <Engine/Toast/Factory.hpp>
#include <Engine/Toast/Objects/Scene.hpp>
#include <Engine/Toast/World.hpp>

namespace toast {

Engine* Engine::m_instance;
float Engine::purge_timer = 0.0f;

void Engine::Run(int argc, char** argv) {
	// Before starting the engine we store the arguments 0x
	// OPTIMIZE: Maybe we can put this on the entrypoint to optimize winmain
	m_arguments.clear();
	m_arguments.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0);
	for (int i = 1; i < argc; ++i) {
		m_arguments.emplace_back(argv[i]);
	}

	Init();
	purge_timer = 0.0f;

	while (!GetShouldClose()) {
		// This is our frame 0x
		PROFILE_ZONE_N("Frame");

		// avoid heavy work if minimized
		if (m_window->IsMinimized()) {
			// Still poll events
			m_window->PollEventsOnly();
			m_windowShouldClose.store(m_window->ShouldClose(), std::memory_order_relaxed);
			// Back off a bit to avoid busy-waiting while minimized
			m_window->WaitEventsTimeout(0.016);    // ~60 FPS
			PROFILE_FRAME;
			continue;
		}

		// Poll OS events early in the frame to reduce input latency
		m_window->PollEventsOnly();

		m_time->Tick();

		m_resourceManager->LoadResourcesMainThread();

		// Ensure any pending Begin calls are executed as early as possible in the frame
		m_gameWorld->RunBeginQueue();

		m_eventSystem->PollEvents();
		m_inputSystem->Tick();

		m_gameWorld->EarlyTick();
		m_gameWorld->Tick();
		m_gameWorld->LateTick();

#ifdef TOAST_EDITOR
		m_gameWorld->EditorTick();
#endif

		m_layerStack->TickLayers();

		Render();

		// Start the ImGui frame, only for editor
#ifdef TOAST_EDITOR
		m_renderer->StartImGuiFrame();
		EditorTick();
		m_renderer->EndImGuiFrame();
#endif

		// Swap after all rendering and UI is done
		m_window->SwapBuffers();

		// DestroyQueue also removes scenes 0x
		m_gameWorld->RunDestroyQueue();

		m_windowShouldClose.store(m_window->ShouldClose(), std::memory_order_relaxed);

		// Purge unused resources from the cache
		if (purge_timer >= 120.0f) {
			purge_timer = 0.0f;
			TOAST_TRACE("Purging unused resources...");
			m_resourceManager->PurgeResources();
		}
		purge_timer += Time::delta();

		PROFILE_FRAME;
	}
	Close();
}

bool Engine::GetShouldClose() const {
	return m_windowShouldClose.load(std::memory_order_relaxed);
}

Engine* Engine::GetInstance() {
	return m_instance;
}

Engine::Engine() {
	if (m_instance) {
		throw ToastException("There is already an instance of Engine");
	}
	m_instance = this;
}

void Engine::Init() {
	// Starting logging system 0x
	Log::Init();
	TOAST_INFO("Initializing Toast Engine...");
	if (!m_arguments.empty()) {
		TOAST_TRACE("Called with {0} arguments", m_arguments.size());
	}

	m_resourceManager = std::make_unique<resource::ResourceManager>(false);
	m_projectSettings = std::make_unique<ProjectSettings>();

	// Starting time tracking
	m_time = std::make_unique<Time>();

	// Starting event system
	m_eventSystem = std::make_unique<event::EventSystem>();

	// Create window
	m_window = std::make_unique<Window>(1920, 1080, "ToastEngine");
	m_layerStack = std::make_unique<renderer::LayerStack>();
	m_renderer = std::make_unique<renderer::OpenGLRenderer>();

	// Create input system
	m_inputSystem = std::make_unique<input::InputSystem>();

	// Create the Game World
	m_gameWorld = std::make_unique<World>();

	// Create the Factory
	m_factory = std::make_unique<Factory>();

	// Imguilayer testing purposes
	m_layerStack->PushLayer(new renderer::DebugDrawLayer());

	Begin();
}

void Engine::EditorTick() { }

void Engine::Render() {
	PROFILE_ZONE;

	m_renderer->Render();
}

void Engine::Close() { }

}
