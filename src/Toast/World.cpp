#include "../../src/Physics/PhysicsSystem.hpp"
#include "Engine/Toast/Objects/Scene.hpp"
#include "spine-cpp-lite.h"

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Profiler.hpp>
#include <Engine/Resources/ResourceManager.hpp>
#include <Engine/Toast/SimulateWorldEvent.hpp>
#include <Engine/Toast/World.hpp>

namespace toast {

World* World::m_instance = nullptr;

World* World::Instance() {
	if (!m_instance) {
		throw ToastException("World doesn't exist yet");
	}
	return m_instance;
}

World::World() {
	if (m_instance) {
		throw ToastException("Having more than one world is not allowed");
	}
	m_instance = this;

	// Event handling
	m_listener = std::make_unique<event::ListenerComponent>();
	m_listener->Subscribe<SceneLoadedEvent>(
	    [this](const SceneLoadedEvent* e) {
		    // Check if scene still exists
		    if (!m_children.Has(e->id)) {
			    return true;
		    }

		    auto* scene = m_children.Get(e->id);
		    if (!scene) {
			    return true;    // Scene is being destroyed
		    }

		    // set the scene as loaded in the list
		    m_tickableScenes[e->id] = dynamic_cast<Scene*>(scene);

#ifdef TOAST_EDITOR
		    {
			    auto* scenePtr = static_cast<Scene*>(scene);
			    scenePtr->_LoadTextures();
		    }
#endif

		    // call the load callback if there is one
		    if (e->loadCallback.has_value()) {
			    auto* scenePtr = static_cast<Scene*>(scene);
			    (*e->loadCallback)(scenePtr);
		    }

		    return true;
	    },
	    2
	);
#ifdef TOAST_EDITOR
	m_listener->Subscribe<SimulateWorldEvent>(
	    [this](const SimulateWorldEvent* e) {
		    OnSimulateWorld(e->value);

		    auto simulate = e->value;
		    if (simulate) {
			    // Logic for play logic
			    for (auto& s : m_children | std::views::values) {
				    auto* scene = static_cast<Scene*>(s.get());
				    s->SoftSave();
				    s->RefreshBegin(true);
				    m_loadedScenes.insert({ s->id(), scene->json_path() });
				    m_loadedScenesStatus.insert({ s->id(), s->enabled() });
			    }
		    } else {
			    // Logic for pause logic
			    m_editorScene->_Begin();    // Rerun to set editor camera

			    for (auto& s : m_children | std::views::values) {
				    if (!m_loadedScenes.contains(s->id())) {
					    // If the scene didn't exist on runtime, we remove it
					    UnloadScene(s->id());
					    continue;
				    }
				    s->SoftLoad();
				    s->enabled(m_loadedScenesStatus[s->id()]);
				    m_loadedScenes.erase(s->id());
			    }

			    for (const auto& [_, path] : m_loadedScenes) {
				    // Create again the remaining scenes
				    LoadScene(path, [](const Scene* s) {
					    EnableScene(s->id());
				    });
			    }

			    m_loadedScenes.clear();
			    m_loadedScenesStatus.clear();
		    }

		    return true;
	    },
	    0
	);
#endif

	// rptr chosen instead of uptr so there is no need of including
	// ThreadPool.hpp on the header, since it's private to the engine
	m_threadPool = new ThreadPool();
	m_threadPool->Init(POOL_SIZE);
}

World::~World() {
	while (m_threadPool->busy()) { }
	m_threadPool->Destroy();
	delete m_threadPool;
}

unsigned World::New(
    const std::string& type, const std::optional<std::string>& name, const std::optional<json_t>& j, const std::function<void(Scene*)>& init_callback
) {
	auto* world = Instance();
	std::string obj_name {};

	auto reg = Object::getRegistry();
	auto* obj = reg[type](world->m_children, std::nullopt);
	auto obj_id = obj->id();

	// Add name to the scene
	if (name.has_value()) {
		obj->m_name = *name;
	} else {
		obj->name(std::format("{0}_{1}", obj->type(), obj->id()));
	}
	obj_name = obj->name();

	// Set values to the parent() and scene() functions
	obj->m_parent = nullptr;
	obj->m_scene = dynamic_cast<Scene*>(obj);
	obj->children.parent(obj);
	obj->children.scene(dynamic_cast<Scene*>(obj));

	// Run load and init
	if (j.has_value()) {
		obj->Load(*j);
	}
	obj->_Init();

	// Schedule the Begin
	ScheduleBegin(obj);

	if (obj->base_type() == SceneT) {
		auto* e = new SceneLoadedEvent { obj_id, obj_name };
		if (init_callback) {
			e->loadCallback = init_callback;
		}
		event::Send(e);
	}

	return obj_id;
}

void World::LoadScene(std::string_view path, std::optional<std::function<void(Scene*)>>&& load_callback) {
	std::string p { path };
	Instance()->m_threadPool->QueueJob([path = p, load_callback = std::move(load_callback)] {
		// Load scene file
		// Use shared_ptr to keep JSON alive until event is processed
		auto j_ptr = std::make_shared<json_t>();
		try {
			*j_ptr = json_t::parse(*resource::Open(path));
		} catch (const std::exception& e) {
			TOAST_ERROR("Failed opening scene with path \"{0}\"\n{1}", path, e.what());
			return;
		}
		if (j_ptr->empty() || !j_ptr->contains("format") || (*j_ptr)["format"] != "scene") {
			TOAST_ERROR("Scene \"{0}\" is empty or invalid", path);
			return;
		}

		auto* world = Instance();
		unsigned scene_id = 0;
		std::string scene_name {};
		{
			// Creating scene from the registry
			// Force string copy to avoid string_view references to JSON
			const std::string scene_type = (*j_ptr)["type"].get<std::string>();
			auto create_registry = Object::getRegistry();
			auto* scene = static_cast<Scene*>(create_registry[scene_type](world->m_children, std::nullopt));
			scene_id = scene->id();

			// Add name to the scene - force copy
			std::string name_copy = (*j_ptr)["name"].get<std::string>();
			scene->name(std::move(name_copy));
			scene_name = scene->name();

			scene->m_parent = nullptr;
			scene->m_scene = scene;
			scene->children.parent(scene);
			scene->children.scene(scene);

			// Run load and init
			const std::string p_str { path };
			scene->Load(p_str);
			scene->_Init();
			scene->enabled(false);

			// Schedule the Begin
			ScheduleBegin(scene);
		}

		auto* e = new SceneLoadedEvent { scene_id, scene_name };
		if (load_callback.has_value()) {
			// If the client added custom logic for the load scene,
			// send the lambda to the event
			e->loadCallback = load_callback;
		}

		// Keep JSON alive by capturing it in the event callback
		event::Send(e);

		// j_ptr will be destroyed here
	});
}

void World::UnloadScene(const unsigned id) {
	auto* w = Instance();

	// Check if scene still exists
	if (!w->m_children.Has(id)) {
		return;    // Already unloaded, nothing to do
	}

	// Get scene safely
	auto* scene = w->m_children.Get(id);
	if (!scene) {
		return;    // Scene doesn't exist or is being destroyed
	}

	// Disable the scene manually
	if (scene->enabled()) {
		scene->_enabled(false);
	}

	// Remove from tickables immediately so it stops being processed
	w->m_tickableScenes.erase(id);

	// Request physics halt
	physics::PhysicsSystem::RequestHaltSimulation();

	// Just schedule for destruction
	w->ScheduleDestroy(scene);
}

void World::UnloadScene(const std::string& name) {
	const auto obj = dynamic_cast<Scene*>(Get(name));
	if (!obj) {
		TOAST_ERROR("Object {0} is not a Scene", name);
		return;
	}
	UnloadScene(obj->id());
}

void World::EnableScene(const std::string& name) {
	auto* w = Instance();
	auto* scene = w->m_children.Get(name);
	if (!scene) {
		TOAST_ERROR("Tried to enable scene \"{0}\" but it doesn't exist", name);
		return;
	}
	EnableScene(scene->id());
}

void World::EnableScene(unsigned id) {
	auto* w = Instance();
	if (!w->m_children.Has(id)) {
		TOAST_ERROR("Tried to activate scene {0} but it doesn't exist", id);
		return;
	}

	// We will throw an exception if the scene hasn't finished loaded so that
	// anyone can implement a custom action for when that happens (i.e. loading screen)
	if (!w->m_tickableScenes.contains(id)) {
		throw BadScene(id);
	}

	auto* scene = w->m_children.Get(id);
	if (!scene) {
		return;    // Scene is being destroyed
	}

	if (scene->enabled()) {
		TOAST_WARN("Tried to activate scene {0} but it's already activated", id);
		return;
	}

	scene->enabled(true);
}

void World::DisableScene(const std::string& name) {
	auto* w = Instance();
	auto* scene = w->m_children.Get(name);
	if (!scene) {
		TOAST_ERROR("Tried to disable scene \"{0}\" but it doesn't exist", name);
		return;
	}
	DisableScene(scene->id());
}

void World::DisableScene(unsigned id) {
	auto* w = Instance();
	if (!w->m_children.Has(id)) {
		TOAST_ERROR("Tried to deactivate scene {0} but it doesn't exist", id);
		return;
	}

	auto* scene = w->m_children.Get(id);
	if (!scene) {
		return;    // Scene is being destroyed
	}

	if (!scene->enabled()) {
		TOAST_WARN("Tried to deactivate scene {0} but it's already deactivated", id);
		return;
	}

	scene->enabled(false);
}

#ifdef TOAST_EDITOR
void World::OnSimulateWorld(const bool value) {
	m_simulateWorld = value;
	// TODO: Handle soft saving and loading
	if (value) {
		// Logic for initiating play logic
	} else {
		// Logic for initiating end logic
	}
}
#endif

#pragma region OBJECT_LOOPS

void World::EarlyTick() {
	if (!m_simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m_tickableScenes) {
		s->_EarlyTick();
	}
}

void World::Tick() {
	if (!m_simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m_tickableScenes) {
		s->_Tick();
	}
}

void World::LateTick() {
	if (!m_simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m_tickableScenes) {
		s->_LateTick();
	}
}

void World::PhysTick() {
	if (!m_simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m_tickableScenes) {
		s->_PhysTick();
	}

#ifdef TOAST_EDITOR
	// NOTE: Idk if we should tick physics on the editor scene -x
	if (m_editorScene != nullptr) {
		m_editorScene->_PhysTick();
	}
#endif
}

#ifdef TOAST_EDITOR
void World::EditorTick() {
	PROFILE_ZONE;

	if (m_editorScene != nullptr) {
		m_editorScene->_EarlyTick();
		m_editorScene->_Tick();
		m_editorScene->_EditorTick();
		m_editorScene->_LateTick();
	}

	// ReSharper disable once CppUseElementsView
	for (const auto& [_, s] : m_children) {
		s->_EditorTick();
	}
}
#endif

void World::RunBeginQueue() {
	if (!m_simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	// Swap the queue into a local list under lock so other threads can enqueue while we process
	std::list<Object*> local {};
	{
		std::lock_guard lock(m_queueMutex);
		local.swap(m_beginQueue);
	}

	for (auto* obj : local) {
		if (!obj) {
			continue;
		}

		// We need to check this so scene begin is not run while the scene is being loaded
		if (!m_tickableScenes.contains(obj->scene()->id())) {
			std::lock_guard lock(m_queueMutex);
			m_beginQueue.push_back(obj);
			continue;
		}

		obj->_Begin();
		// If begin didn't run, reschedule it for later
		if (!obj->has_run_begin()) {
			std::lock_guard lock(m_queueMutex);
			m_beginQueue.push_back(obj);
		}
	}
}

void World::RunDestroyQueue() {
	PROFILE_ZONE;

	// Move the destroy queue into a local list under lock and process without holding the lock
	std::list<Object*> local {};
	{
		std::lock_guard lock(m_queueMutex);
		local.swap(m_destroyQueue);
	}

	if (local.empty()) {
		return;
	}

	for (auto* obj : local) {
		if (!obj) {
			continue;
		}

		// Call _Destroy() - it will handle double-destruction internally
		obj->_Destroy();

		// Remove the object from its parent's children map
		if (obj->parent()) {
			obj->parent()->children.erase(obj->id());
		} else {
			// If it's a root-level object (likely a scene), remove from world's children
			if (obj->base_type() == SceneT) {
				// Wait for physics confirmation (with timeout to avoid infinite rescheduling)
				int retries = 0;
				const int MAX_RETRIES = 100;
				while (!physics::PhysicsSystem::WaitForAnswer() && retries < MAX_RETRIES) {
					retries++;
					// Busy wait for physics
				}

				if (retries >= MAX_RETRIES) {
					// Physics didn't respond - force erase anyway
					TOAST_WARN("Physics didn't respond after {0} retries, force erasing scene {1}", MAX_RETRIES, obj->id());
					m_children.erase(obj->id());
				} else {
					// Physics confirmed
					m_children.erase(obj->id());
					physics::PhysicsSystem::ReceivedAnswer();
				}
			} else {
				// Unexpected: object without parent that's not a scene - still attempt to erase from world
				m_children.erase(obj->id());
			}
		}
	}
}

void World::ScheduleBegin(Object* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	std::lock_guard lock(w->m_queueMutex);
	w->m_beginQueue.push_back(obj);
}

void World::CancelBegin(Object* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	const auto it = std::ranges::find(w->m_beginQueue, obj);
	if (it != w->m_beginQueue.end()) {
		std::lock_guard lock(w->m_queueMutex);
		w->m_beginQueue.erase(it);
	}
}

void World::ScheduleDestroy(Object* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	std::lock_guard lock(w->m_queueMutex);
	w->m_destroyQueue.push_back(obj);
}

const std::list<Object*>& World::begin_queue() const {
	return m_beginQueue;
}

#ifdef TOAST_EDITOR
void World::SetEditorScene(Object* obj) {
	m_editorScene = obj;

	obj->m_name = "EditorScene";
	obj->m_id = Factory::AssignId();

	obj->m_parent = nullptr;
	obj->m_scene = dynamic_cast<Scene*>(obj);
	obj->children.parent(obj);
	obj->children.scene(dynamic_cast<Scene*>(obj));

	m_editorScene->_Init();
	m_editorScene->_LoadTextures();
	m_editorScene->enabled(true);
	m_editorScene->_Begin(true);
}
#endif

Object* World::Get(const unsigned id) {
	return Instance()->m_children.Get(id);
}

Object* World::Get(const std::string& name) {
	return Instance()->m_children.Get(name);
}

bool World::Has(const unsigned id) {
	return Instance()->m_children.Has(id);
}

bool World::Has(const std::string& name) {
	return Instance()->m_children.Has(name);
}

Object::Children& World::GetChildren() {
	return Instance()->m_children;
}

#pragma endregion

}
