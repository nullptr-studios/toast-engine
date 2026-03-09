#include "Toast/World.hpp"

#include "Physics/PhysicsSystem.hpp"
#include "Toast/Log.hpp"
#include "Toast/Nodes/RootNode.hpp"
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
	m.listener = std::make_unique<event::ListenerSubNode>();
	m.listener->Subscribe<RootNodeLoadedEvent>(
	    [this](const RootNodeLoadedEvent* e) {
		    // Check if scene still exists
		    if (!m.children.Has(e->id)) {
			    return true;
		    }

		    auto* scene = m.children.Get(e->id);
		    if (!scene) {
			    return true;    // RootNode is being destroyed
		    }

		    // set the scene as loaded in the list
		    m.tickableRootNodes[e->id] = dynamic_cast<RootNode*>(scene);

#ifdef TOAST_EDITOR
		    {
			    auto* scene_ptr = static_cast<RootNode*>(scene);
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
				    auto* scene = static_cast<RootNode*>(s.get());
				    s->SoftSave();
				    s->RefreshBegin(true);
				    m.loadedRootNodes.insert({ s->id(), scene->json_path() });
				    m.loadedRootNodesStatus.insert({ s->id(), s->enabled() });
			    }

			    physics::PhysicsSystem::start();
		    } else {
			    // Logic for pause logic
			    physics::PhysicsSystem::stop();
			    m.editorRootNode->_Begin();    // Rerun to set editor camera

			    for (auto& s : m.children | std::views::values) {
				    if (!m.loadedRootNodes.contains(s->id())) {
					    // If the scene didn't exist on runtime, we remove it
					    UnloadRootNode(s->id());
					    continue;
				    }

				    s->SoftLoad();
				    s->enabled(m.loadedRootNodesStatus[s->id()]);
				    m.loadedRootNodes.erase(s->id());
			    }

			    for (const auto& [_, path] : m.loadedRootNodes) {
				    // Create again the remaining scenes
				    LoadRootNodeSync(path);
			    }

			    m.loadedRootNodes.clear();
			    m.loadedRootNodesStatus.clear();
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

Node* World::New(std::string_view type, const std::optional<std::string>& name) {
	auto* world = Instance();
	std::string obj_name {};

	auto reg = Node::getRegistry();
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
	obj->m_scene = dynamic_cast<RootNode*>(obj);
	obj->children.parent(obj);
	obj->children.scene(dynamic_cast<RootNode*>(obj));

	// Run load and init
	obj->_Init();

	// Schedule the Begin
	ScheduleBegin(obj);

	if (obj->base_type() == RootNodeT) {
		auto* e = new RootNodeLoadedEvent { obj_id, obj_name };
		event::Send(reinterpret_cast<event::IEvent*>(e));
	}

	return obj;
}

auto World::LoadRootNode(std::string_view path) -> std::future<unsigned> {
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
			TOAST_ERROR("RootNode \"{0}\" is empty or invalid", path);
			return;
		}

		auto* world = Instance();
		unsigned scene_id = 0;
		std::string scene_name {};
		{
			// Creating scene from the registry
			// Force string copy to avoid string_view references to JSON
			const std::string scene_type = j["type"].get<std::string>();
			auto create_registry = Node::getRegistry();
			auto* scene = static_cast<RootNode*>(create_registry[scene_type](world->m.children, std::nullopt));
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

		auto* e = new RootNodeLoadedEvent { scene_id, scene_name };
		event::Send(reinterpret_cast<event::IEvent*>(e));
		promis->set_value(scene_id);
	});
	return futur;
}

void World::LoadRootNodeSync(std::string_view path) {
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
		TOAST_ERROR("RootNode \"{0}\" is empty or invalid", path);
		return;
	}

	auto* world = Instance();
	unsigned scene_id = 0;
	std::string scene_name {};
	{
		// Creating scene from the registry
		// Force string copy to avoid string_view references to JSON
		const std::string scene_type = j["type"].get<std::string>();
		auto create_registry = Node::getRegistry();
		auto* scene = static_cast<RootNode*>(create_registry[scene_type](world->m.children, std::nullopt));
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

	auto* e = new RootNodeLoadedEvent { scene_id, scene_name };
	event::Send(reinterpret_cast<event::IEvent*>(e));
}

void World::UnloadRootNode(const unsigned id) {
	auto* w = Instance();

	// Check if scene still exists
	if (!w->m.children.Has(id)) {
		return;    // Already unloaded, nothing to do
	}

	// Get scene safely
	auto* scene = w->m.children.Get(id);
	if (!scene) {
		return;    // RootNode doesn't exist or is being destroyed
	}

	// Disable the scene manually
	if (scene->enabled()) {
		scene->_enabled(false);
	}

	// Remove from tickables immediately so it stops being processed
	w->m.tickableRootNodes.erase(id);

	// Just schedule for destruction
	toast::World::ScheduleDestroy(scene);
}

void World::UnloadRootNode(std::string_view name) {
	auto* const obj = dynamic_cast<RootNode*>(Get(name));
	if (!obj) {
		TOAST_ERROR("Node {0} is not a RootNode", name);
		return;
	}
	UnloadRootNode(obj->id());
}

void World::EnableRootNode(std::string_view name) {
	auto* w = Instance();
	auto* scene = w->m.children.Get(name);
	if (!scene) {
		TOAST_ERROR("Tried to enable scene \"{0}\" but it doesn't exist", name);
		return;
	}
	EnableRootNode(scene->id());
}

void World::EnableRootNode(unsigned id) {
	auto* w = Instance();
	if (!w->m.children.Has(id)) {
		TOAST_ERROR("Tried to activate scene {0} but it doesn't exist", id);
		return;
	}

	// We will throw an exception if the scene hasn't finished loaded so that
	// anyone can implement a custom action for when that happens (i.e. loading screen)
	if (!w->m.tickableRootNodes.contains(id)) {
		throw BadRootNode(id);
	}

	auto* scene = w->m.children.Get(id);
	if (!scene) {
		return;    // RootNode is being destroyed
	}

	if (scene->enabled()) {
		TOAST_WARN("Tried to activate scene {0} but it's already activated", id);
		return;
	}

	scene->enabled(true);
}

void World::DisableRootNode(std::string_view name) {
	auto* w = Instance();
	auto* scene = w->m.children.Get(name);
	if (!scene) {
		TOAST_ERROR("Tried to disable scene \"{0}\" but it doesn't exist", name);
		return;
	}
	DisableRootNode(scene->id());
}

void World::DisableRootNode(unsigned id) {
	auto* w = Instance();
	if (!w->m.children.Has(id)) {
		TOAST_ERROR("Tried to deactivate scene {0} but it doesn't exist", id);
		return;
	}

	auto* scene = w->m.children.Get(id);
	if (!scene) {
		return;    // RootNode is being destroyed
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

	for (const auto& [_, s] : m.tickableRootNodes) {
		s->_EarlyTick();
	}
}

void World::Tick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m.tickableRootNodes) {
		s->_Tick();
	}
}

void World::LateTick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m.tickableRootNodes) {
		s->_LateTick();
	}
}

void World::PhysTick() {
	if (!m.simulateWorld) {
		return;
	}

	PROFILE_ZONE;

	for (const auto& [_, s] : m.tickableRootNodes) {
		s->_PhysTick();
	}

#ifdef TOAST_EDITOR
	// NOTE: Idk if we should tick physics on the editor scene -x
	if (m.editorRootNode != nullptr) {
		m.editorRootNode->_PhysTick();
	}
#endif
}

#ifdef TOAST_EDITOR
void World::EditorTick() {
	PROFILE_ZONE;

	if (m.editorRootNode != nullptr) {
		m.editorRootNode->_EarlyTick();
		m.editorRootNode->_Tick();
		m.editorRootNode->_EditorTick();
		m.editorRootNode->_LateTick();
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
	std::list<Node*> local {};
	{
		std::lock_guard lock(m.queueMutex);
		local.swap(m.beginQueue);
	}

	for (auto* obj : local) {
		if (!obj) {
			continue;
		}

		// We need to check this so scene begin is not run while the scene is being loaded
		if (!m.tickableRootNodes.contains(obj->scene()->id())) {
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
	PROFILE_ZONE_C(0xFF0080);    // Pink for destroy queue

	// Move the destroy queue into a local list under lock and process without holding the lock
	std::list<Node*> local {};
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

void World::ScheduleBegin(Node* obj) {
	if (!obj) {
		return;
	}
	auto* w = Instance();
	std::lock_guard lock(w->m.queueMutex);
	w->m.beginQueue.push_back(obj);
}

void World::CancelBegin(Node* obj) {
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

void World::ScheduleDestroy(Node* obj) {
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

const std::list<Node*>& World::begin_queue() const {
	return m.beginQueue;
}

#ifdef TOAST_EDITOR
void World::SetEditorRootNode(Node* obj) {
	m.editorRootNode = obj;

	obj->m_name = "EditorRootNode";
	obj->m_id = Factory::AssignId();

	obj->m_parent = nullptr;
	obj->m_scene = dynamic_cast<RootNode*>(obj);
	obj->children.parent(obj);
	obj->children.scene(dynamic_cast<RootNode*>(obj));

	m.editorRootNode->_Init();
	m.editorRootNode->_LoadTextures();
	m.editorRootNode->enabled(true);
	m.editorRootNode->_Begin(true);
}
#endif

Node* World::Get(const unsigned id) {
	return Instance()->m.children.Get(id);
}

Node* World::Get(std::string_view name) {
	return Instance()->m.children.Get(name);
}

auto World::GetFromType(std::string_view type) -> Node* {
	return Instance()->m.children.GetType(type, true);
}

bool World::Has(const unsigned id) {
	return Instance()->m.children.Has(id);
}

bool World::Has(std::string_view name) {
	return Instance()->m.children.Has(name);
}

Node::Children& World::GetChildren() {
	return Instance()->m.children;
}

#pragma endregion

}
