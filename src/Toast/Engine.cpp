#include "Toast/Engine.hpp"

#include "Audio/AudioSystem.hpp"
#include "Event/EventSystem.hpp"
#include "ForceLink.cpp"
#include "Input/InputSystem.hpp"
#include "Physics/PhysicsSystem.hpp"
#include "Toast/Factory.hpp"
#include "Toast/Log.hpp"
#include "Toast/Objects/Scene.hpp"
#include "Toast/ProjectSettings.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Renderer/LayerStack.hpp"
#include "Toast/Renderer/OpenGL/OpenGLRenderer.hpp"
#include "Toast/Time.hpp"
#include "Toast/Window/Window.hpp"
#include "Toast/World.hpp"

#include <memory>

#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

namespace toast {

Engine* Engine::m_instance;
double Engine::purge_timer = 0.0;

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
	audio::AudioSystem* audioSystem;
};

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

	auto* window = m->window.get();
	auto* world = m->gameWorld.get();
	while (!GetShouldClose()) {
		// This is our frame 0x
		PROFILE_ZONE_N("Frame");

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

		// Ensure any pending Begin calls are executed as early as possible in the frame
		world->RunBeginQueue();

		m->eventSystem->PollEvents();
		m->inputSystem->Tick();

		world->EarlyTick();

		// Interpolate rb transforms before rendering
		physics::PhysicsSystem::UpdateVisualInterpolation();

		world->Tick();
		world->LateTick();

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
	m->window = std::make_unique<Window>(1920, 1080, "ToastEngine");
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

	Begin();
}

void Engine::EditorTick() { }

void Engine::Render() {
	PROFILE_ZONE;
	m->renderer->Render();
}

void Engine::Close() {
	delete m;
}

void Engine::ForcePurgeResources() {
	// Force purge by setting timer to a very old value
	purge_timer = -200.0;
}

}
