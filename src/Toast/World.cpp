#include "Toast/World.hpp"

#include "Audio/AudioSystem.hpp"
#include "Physics/PhysicsSystem.hpp"
#include "Toast/Audio/Audio.hpp"
#include "Toast/Log.hpp"
#include "Toast/Objects/Scene.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/SimulateWorldEvent.hpp"
#include "Toast/ThreadPool.hpp"

#include <exception>
#include <functional>
#include <future>
#include <memory>

namespace toast {

World* World::m_instance = nullptr;

World* World::Instance() {
	if (!m_instance) {
		throw ToastException("World doesn't exist yet");
	}
	return m_instance;
}

World::World() {
	PROFILE_ZONE_N("World Construction");

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

		    auto* scene_ptr = dynamic_cast<Scene*>(scene);
		    m.tickableScenesIndex[e->id] = m.tickableScenes.size();
		    m.tickableScenes.push_back(scene_ptr);

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
		    	audio::unmute_all();
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
		    	
		    	audio::mute_all();
		    	
			    // Logic for pause logic
			    physics::PhysicsSystem::stop();
			    m.editorScene->_Begin();    // Rerun to set editor camera

			    for (auto& s : m.children | std::views::values) {
				    if (!m.loadedScenes.contains(s->id())) {
					    // If the scene didn't exist on runtime, we remove it
					    UnloadScene(s->id());
					    continue;
				    }

				    s->SoftLoad(true);
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

bool World::IsRunning() {
	auto* w = Instance();

	if (not w) {
		return false;
	}

	return w->m.simulateWorld;
}

Object* World::New(std::string_view type, const std::optional<std::string>& name) {
	auto* world = Instance();
	std::string obj_name {};

	auto reg = Object::getRegistry();
	auto* obj = reg[type.data()](world->m.children, std::nullopt);
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
		event::Send(reinterpret_cast<event::IEvent*>(e));
	}

	return obj;
}

auto World::LoadScene(std::string_view path) -> std::future<unsigned> {
	std::shared_ptr<std::promise<unsigned>> promis = std::make_shared<std::promise<unsigned>>();
	std::future<unsigned> futur = promis->get_future();
	std::string p { path };
	std::function<void()> llambda = []() mutable { };
	Instance()->m.threadPool->QueueJob([path = p, promis] {
		// Load scene file
		json_t j;
		try {
			auto p = resource::Open(path);
			if (!p.has_value()) {
				throw ToastException("Cannot open scene file: " + std::string(path));
			}
			j = json_t::parse(p.value());
		} catch (std::exception& e) {
			TOAST_ERROR("Failed opening scene with path \"{0}\"\n{1}", path, e.what());
			promis->set_exception(std::current_exception());
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
		event::Send(reinterpret_cast<event::IEvent*>(e));
		promis->set_value(scene_id);
	});
	return futur;
}

void World::LoadSceneSync(std::string_view path) {
	PROFILE_ZONE_C(0x0080FF);    // Light blue for sync scene loading
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

		// Load Sync should enable the scene by default
		scene->enabled(true);

		// Schedule the Begin
		ScheduleBegin(scene);
	}

	auto* e = new SceneLoadedEvent { scene_id, scene_name };
	event::Send(reinterpret_cast<event::IEvent*>(e));
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

	// Just schedule for destruction
	toast::World::ScheduleDestroy(scene);
}

void World::UnloadScene(std::string_view name) {
	auto* const obj = dynamic_cast<Scene*>(Get(name));
	if (!obj) {
		TOAST_ERROR("Object {0} is not a Scene", name);
		return;
	}
	UnloadScene(obj->id());
}

void World::EnableScene(std::string_view name) {
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
	if (!w->m.tickableScenesIndex.contains(id)) {
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

void World::DisableScene(std::string_view name) {
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

	for (Scene* scene : m.tickableScenes) {
		scene->_EarlyTick();
	}
}

void World::Tick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (Scene* scene : m.tickableScenes) {
		scene->_Tick();
	}
}

void World::LateTick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (Scene* scene : m.tickableScenes) {
		scene->_LateTick();
	}
}

void World::PhysTick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (Scene* scene : m.tickableScenes) {
		scene->_PhysTick();
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

	for (Scene* scene : m.tickableScenes) {
		scene->_EditorTick();
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

	std::list<Object*> requeue_list {};

	for (Object* obj : local) {
		if (!obj) {
			continue;
		}

		// We need to check this so scene begin is not run while the scene is being loaded
		if (obj->scene() && !m.tickableScenesIndex.contains(obj->scene()->id())) {
			requeue_list.push_back(obj);
			continue;
		}

		obj->_Begin();
		// If begin didn't run, reschedule it for later
		if (!obj->has_run_begin()) {
			requeue_list.push_back(obj);
		}
	}

	// Batch requeue if needed
	if (!requeue_list.empty()) {
		std::lock_guard lock(m.queueMutex);
		m.beginQueue.splice(m.beginQueue.end(), requeue_list);
	}
}

void World::RunDestroyQueue() {
	PROFILE_ZONE_C(0xFF0080);    // Pink for destroy queue

	// Move the destroy queue into a local list under lock and process without holding the lock
	std::list<Object*> local {};
	{
		std::lock_guard lock(m.queueMutex);
		local.swap(m.destroyQueue);
	}

	if (local.empty()) {
		return;
	}

	std::vector<size_t> indices_to_remove {};
	indices_to_remove.reserve(local.size());

	for (Object* obj : local) {
		if (!obj) {
			continue;
		}

		// Call _Destroy() - it will handle double-destruction internally
		obj->_Destroy();

		// Remove the object from its parent's children map
		if (obj->parent()) {
			obj->parent()->children.erase(obj->id());
		} else {
			const auto idx_it = m.tickableScenesIndex.find(obj->id());
			if (idx_it != m.tickableScenesIndex.end()) {
				indices_to_remove.push_back(idx_it->second);
				m.tickableScenesIndex.erase(idx_it);
			}
			m.children.erase(obj->id());
		}
	}

	if (!indices_to_remove.empty()) {
		std::sort(indices_to_remove.rbegin(), indices_to_remove.rend());
		for (size_t idx : indices_to_remove) {
			if (idx < m.tickableScenes.size()) {
				// Update index map for moved element
				Scene* moved_scene = m.tickableScenes.back();
				if (idx != m.tickableScenes.size() - 1) {
					m.tickableScenes[idx] = moved_scene;
					m.tickableScenesIndex[moved_scene->id()] = idx;
				}
				m.tickableScenes.pop_back();
			}
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
	std::lock_guard lock(w->m.queueMutex);
	const auto it = std::ranges::find(w->m.beginQueue, obj);
	if (it != w->m.beginQueue.end()) {
		w->m.beginQueue.erase(it);
	}
}

void World::ScheduleDestroy(Object* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	std::lock_guard lock(w->m.queueMutex);
	// Avoid double-scheduling the same object for destruction
	if (std::ranges::find(w->m.destroyQueue, obj) == w->m.destroyQueue.end()) {
		w->m.destroyQueue.push_back(obj);
	}
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

Object* World::Get(std::string_view name) {
	return Instance()->m.children.Get(name);
}

auto World::GetFromType(std::string_view type) -> Object* {
	return Instance()->m.children.GetType(type, true);
}

bool World::Has(const unsigned id) {
	return Instance()->m.children.Has(id);
}

bool World::Has(std::string_view name) {
	return Instance()->m.children.Has(name);
}

Object::Children& World::GetChildren() {
	return Instance()->m.children;
}

#pragma endregion

}
