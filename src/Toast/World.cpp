#include "Engine/Toast/Objects/Scene.hpp"
#include "Physics/PhysicsSystem.hpp"

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Profiler.hpp>
#include <Engine/Core/ThreadPool.hpp>
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
	m.listener = std::make_unique<event::ListenerComponent>();
	m.listener->Subscribe<SceneLoadedEvent>(
	    [this](const SceneLoadedEvent* e) {
		    // Check if scene still exists
		    if (!m.children.Has(e->id)) {
			    return true;
		    }

		    auto* scene = m.children.Get(e->id);
		    if (!scene) {
			    return true;    // Scene is being destroyed
		    }

		    // set the scene as loaded in the list
		    m.tickableScenes[e->id] = dynamic_cast<Scene*>(scene);

#ifdef TOAST_EDITOR
		    {
			    auto* scene_ptr = static_cast<Scene*>(scene);
			    scene_ptr->_LoadTextures();
		    }
#endif

		    return true;
	    },
	    2
	);
#ifdef TOAST_EDITOR
	m.listener->Subscribe<SimulateWorldEvent>(
	    [this](const SimulateWorldEvent* e) {
		    OnSimulateWorld(e->value);

		    auto simulate = e->value;
		    if (simulate) {
			    // Logic for play logic
			    for (auto& s : m.children | std::views::values) {
				    auto* scene = static_cast<Scene*>(s.get());
				    s->SoftSave();
				    s->RefreshBegin(true);
				    m.loadedScenes.insert({ s->id(), scene->json_path() });
				    m.loadedScenesStatus.insert({ s->id(), s->enabled() });
			    }

			    physics::PhysicsSystem::start();
		    } else {
			    // Logic for pause logic
			    physics::PhysicsSystem::stop();
			    m.editorScene->_Begin();    // Rerun to set editor camera

			    for (auto& s : m.children | std::views::values) {
				    if (!m.loadedScenes.contains(s->id())) {
					    // If the scene didn't exist on runtime, we remove it
					    UnloadScene(s->id());
					    continue;
				    }

				    s->SoftLoad();
				    s->enabled(m.loadedScenesStatus[s->id()]);
				    m.loadedScenes.erase(s->id());
			    }

			    for (const auto& [_, path] : m.loadedScenes) {
				    // Create again the remaining scenes
				    LoadSceneSync(path);
			    }

			    m.loadedScenes.clear();
			    m.loadedScenesStatus.clear();
		    }

		    return true;
	    },
	    0
	);
#endif

	// rptr chosen instead of uptr so there is no need of including
	// ThreadPool.hpp on the header, since it's private to the engine
	m.threadPool = new ThreadPool();
	m.threadPool->Init(POOL_SIZE);
}

World::~World() {
	while (m.threadPool->busy()) { }
	m.threadPool->Destroy();
	delete m.threadPool;
}

Object* World::New(const std::string& type, const std::optional<std::string>& name) {
	auto* world = Instance();
	std::string obj_name {};

	auto reg = Object::getRegistry();
	auto* obj = reg[type](world->m.children, std::nullopt);
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
	obj->_Init();

	// Schedule the Begin
	ScheduleBegin(obj);

	if (obj->base_type() == SceneT) {
		auto* e = new SceneLoadedEvent { obj_id, obj_name };
		event::Send(e);
	}

	return obj;
}

void World::LoadScene(std::string_view path) {
	std::string p { path };
	Instance()->m.threadPool->QueueJob([path = p] {
		// Load scene file
		json_t j;
		try {
			j = json_t::parse(*resource::Open(path));
		} catch (const std::exception& e) {
			TOAST_ERROR("Failed opening scene with path \"{0}\"\n{1}", path, e.what());
			return;
		}

		if (j.empty() || !j.contains("format") || j["format"] != "scene") {
			TOAST_ERROR("Scene \"{0}\" is empty or invalid", path);
			return;
		}

		auto* world = Instance();
		unsigned scene_id = 0;
		std::string scene_name {};
		{
			// Creating scene from the registry
			// Force string copy to avoid string_view references to JSON
			const std::string scene_type = j["type"].get<std::string>();
			auto create_registry = Object::getRegistry();
			auto* scene = static_cast<Scene*>(create_registry[scene_type](world->m.children, std::nullopt));
			scene_id = scene->id();

			// Add name to the scene - force copy
			std::string name = j["name"].get<std::string>();
			scene->name(std::move(name));
			scene_name = scene->name();

			scene->m_parent = nullptr;
			scene->m_scene = scene;
			scene->children.parent(scene);
			scene->children.scene(scene);

			// Run load and init
			const std::string& p_str { path };
			scene->Load(p_str);
			scene->_Init();
			scene->enabled(false);

			// Schedule the Begin
			ScheduleBegin(scene);
		}

		auto* e = new SceneLoadedEvent { scene_id, scene_name };
		event::Send(e);
	});
}

void World::LoadSceneSync(std::string_view path) {
	std::string p { path };

	// Load scene file
	json_t j;
	try {
		j = json_t::parse(*resource::Open(p));
	} catch (const std::exception& e) {
		TOAST_ERROR("Failed opening scene with path \"{0}\"\n{1}", path, e.what());
		return;
	}

	if (j.empty() || !j.contains("format") || j["format"] != "scene") {
		TOAST_ERROR("Scene \"{0}\" is empty or invalid", path);
		return;
	}

	auto* world = Instance();
	unsigned scene_id = 0;
	std::string scene_name {};
	{
		// Creating scene from the registry
		// Force string copy to avoid string_view references to JSON
		const std::string scene_type = j["type"].get<std::string>();
		auto create_registry = Object::getRegistry();
		auto* scene = static_cast<Scene*>(create_registry[scene_type](world->m.children, std::nullopt));
		scene_id = scene->id();

		std::string name = j["name"].get<std::string>();
		scene->name(std::move(name));
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
	event::Send(e);
}

void World::UnloadScene(const unsigned id) {
	auto* w = Instance();

	// Check if scene still exists
	if (!w->m.children.Has(id)) {
		return;    // Already unloaded, nothing to do
	}

	// Get scene safely
	auto* scene = w->m.children.Get(id);
	if (!scene) {
		return;    // Scene doesn't exist or is being destroyed
	}

	// Disable the scene manually
	if (scene->enabled()) {
		scene->_enabled(false);
	}

	// Remove from tickables immediately so it stops being processed
	w->m.tickableScenes.erase(id);

	// Just schedule for destruction
	toast::World::ScheduleDestroy(scene);
}

void World::UnloadScene(const std::string& name) {
	auto* const obj = dynamic_cast<Scene*>(Get(name));
	if (!obj) {
		TOAST_ERROR("Object {0} is not a Scene", name);
		return;
	}
	UnloadScene(obj->id());
}

void World::EnableScene(const std::string& name) {
	auto* w = Instance();
	auto* scene = w->m.children.Get(name);
	if (!scene) {
		TOAST_ERROR("Tried to enable scene \"{0}\" but it doesn't exist", name);
		return;
	}
	EnableScene(scene->id());
}

void World::EnableScene(unsigned id) {
	auto* w = Instance();
	if (!w->m.children.Has(id)) {
		TOAST_ERROR("Tried to activate scene {0} but it doesn't exist", id);
		return;
	}

	// We will throw an exception if the scene hasn't finished loaded so that
	// anyone can implement a custom action for when that happens (i.e. loading screen)
	if (!w->m.tickableScenes.contains(id)) {
		throw BadScene(id);
	}

	auto* scene = w->m.children.Get(id);
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
	auto* scene = w->m.children.Get(name);
	if (!scene) {
		TOAST_ERROR("Tried to disable scene \"{0}\" but it doesn't exist", name);
		return;
	}
	DisableScene(scene->id());
}

void World::DisableScene(unsigned id) {
	auto* w = Instance();
	if (!w->m.children.Has(id)) {
		TOAST_ERROR("Tried to deactivate scene {0} but it doesn't exist", id);
		return;
	}

	auto* scene = w->m.children.Get(id);
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
	m.simulateWorld = value;
}
#endif

#pragma region OBJECT_LOOPS

void World::EarlyTick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m.tickableScenes) {
		s->_EarlyTick();
	}
}

void World::Tick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m.tickableScenes) {
		s->_Tick();
	}
}

void World::LateTick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m.tickableScenes) {
		s->_LateTick();
	}
}

void World::PhysTick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m.tickableScenes) {
		s->_PhysTick();
	}

#ifdef TOAST_EDITOR
	// NOTE: Idk if we should tick physics on the editor scene -x
	if (m.editorScene != nullptr) {
		m.editorScene->_PhysTick();
	}
#endif
}

#ifdef TOAST_EDITOR
void World::EditorTick() {
	PROFILE_ZONE;

	if (m.editorScene != nullptr) {
		m.editorScene->_EarlyTick();
		m.editorScene->_Tick();
		m.editorScene->_EditorTick();
		m.editorScene->_LateTick();
	}

	// ReSharper disable once CppUseElementsView
	for (const auto& [_, s] : m.children) {
		s->_EditorTick();
	}
}
#endif

void World::RunBeginQueue() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	// Swap the queue into a local list under lock so other threads can enqueue while we process
	std::list<Object*> local {};
	{
		std::lock_guard lock(m.queueMutex);
		local.swap(m.beginQueue);
	}

	for (auto* obj : local) {
		if (!obj) {
			continue;
		}

		// We need to check this so scene begin is not run while the scene is being loaded
		if (!m.tickableScenes.contains(obj->scene()->id())) {
			std::lock_guard lock(m.queueMutex);
			m.beginQueue.push_back(obj);
			continue;
		}

		obj->_Begin();
		// If begin didn't run, reschedule it for later
		if (!obj->has_run_begin()) {
			std::lock_guard lock(m.queueMutex);
			m.beginQueue.push_back(obj);
		}
	}
}

void World::RunDestroyQueue() {
	PROFILE_ZONE;

	// Move the destroy queue into a local list under lock and process without holding the lock
	std::list<Object*> local {};
	{
		std::lock_guard lock(m.queueMutex);
		local.swap(m.destroyQueue);
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
			m.children.erase(obj->id());
		}
	}
}

void World::ScheduleBegin(Object* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	std::lock_guard lock(w->m.queueMutex);
	w->m.beginQueue.push_back(obj);
}

void World::CancelBegin(Object* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	const auto it = std::ranges::find(w->m.beginQueue, obj);
	if (it != w->m.beginQueue.end()) {
		std::lock_guard lock(w->m.queueMutex);
		w->m.beginQueue.erase(it);
	}
}

void World::ScheduleDestroy(Object* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	std::lock_guard lock(w->m.queueMutex);
	w->m.destroyQueue.push_back(obj);
}

const std::list<Object*>& World::begin_queue() const {
	return m.beginQueue;
}

#ifdef TOAST_EDITOR
void World::SetEditorScene(Object* obj) {
	m.editorScene = obj;

	obj->m_name = "EditorScene";
	obj->m_id = Factory::AssignId();

	obj->m_parent = nullptr;
	obj->m_scene = dynamic_cast<Scene*>(obj);
	obj->children.parent(obj);
	obj->children.scene(dynamic_cast<Scene*>(obj));

	m.editorScene->_Init();
	m.editorScene->_LoadTextures();
	m.editorScene->enabled(true);
	m.editorScene->_Begin(true);
}
#endif

Object* World::Get(const unsigned id) {
	return Instance()->m.children.Get(id);
}

Object* World::Get(const std::string& name) {
	return Instance()->m.children.Get(name);
}

bool World::Has(const unsigned id) {
	return Instance()->m.children.Has(id);
}

bool World::Has(const std::string& name) {
	return Instance()->m.children.Has(name);
}

Object::Children& World::GetChildren() {
	return Instance()->m.children;
}

#pragma endregion

}
