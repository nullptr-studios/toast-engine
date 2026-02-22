#pragma once
#include "Objects/Object.hpp"
#include "Toast/Event/ListenerComponent.hpp"
#include "Toast/GameFlow.hpp"
#include "Toast/SceneLoadedEvent.hpp"

#include <future>
#include <mutex>
#include <optional>
#include <string_view>

namespace toast {

class ThreadPool;

class World {
public:
	World();
	~World();

	// Delete copy and move semantics
	World(const World&) = delete;
	World& operator=(const World&) = delete;
	World(World&&) = delete;
	World& operator=(World&&) = delete;

	static World* Instance();

	template<typename T>
	static auto New(const std::optional<std::string>& name = std::nullopt) -> T*;
	static auto New(std::string_view type, const std::optional<std::string>& name = std::nullopt) -> Object*;
	static auto LoadScene(std::string_view path) -> std::future<unsigned>;    ///< Loads scene on the init thread, scene disabld after load
	static void LoadSceneSync(std::string_view path);                         ///< Loads scene on the main thread, scene enabled after load
	static void UnloadScene(unsigned id);
	static void UnloadScene(std::string_view name);
	static void EnableScene(unsigned id);
	static void EnableScene(std::string_view name);
	static void DisableScene(unsigned id);
	static void DisableScene(std::string_view name);

	void EarlyTick();
	void Tick();
	void LateTick();
	void PhysTick();
#ifdef TOAST_EDITOR
	void EditorTick();
#endif
	void RunBeginQueue();
	void RunDestroyQueue();

	template<typename T>
	[[nodiscard]]
	static T* Get(const unsigned id) {
		return static_cast<T*>(Get(id));
	}

	template<typename T>
	[[nodiscard]]
	static T* Get(std::string_view name) {
		return static_cast<T*>(Get(name));
	}

	template<typename T>
	[[nodiscard]]
	static auto GetFromType(std::string_view type) -> T* {
		return static_cast<T*>(GetFromType(type));
	}


	[[nodiscard]]
	static Object* Get(unsigned id);
	[[nodiscard]]
	static Object* Get(std::string_view name);
	[[nodiscard]]
	static bool Has(unsigned id);
	[[nodiscard]]
	static bool Has(std::string_view name);
	[[nodiscard]]
	static Object::Children& GetChildren();

	[[nodiscard]]
	static auto GetFromType(std::string_view type) -> Object*;

	static void ScheduleBegin(Object* obj);
	static void CancelBegin(Object* obj);
	static void ScheduleDestroy(Object* obj);
	[[nodiscard]]
	const std::list<Object*>& begin_queue() const;

#ifdef TOAST_EDITOR
	void SetEditorScene(Object* obj);
#endif

private:
	void OnSimulateWorld(bool value);

	static World* m_instance;
	constexpr static unsigned char DESTROY_SCENE_DELAY = 10;
	constexpr static size_t POOL_SIZE = 2;

	struct {
		Object::Children children;
		std::unique_ptr<event::ListenerComponent> listener;
		std::unordered_map<unsigned, Scene*> tickableScenes;
		std::unordered_map<unsigned, unsigned> sceneDestroyTimers;
		ThreadPool* threadPool = nullptr;
		bool simulateWorld = true;
		std::map<unsigned, std::string> loadedScenes;
		std::map<unsigned, bool> loadedScenesStatus;
		std::list<Object*> beginQueue;
		std::list<Object*> destroyQueue;
		std::mutex queueMutex;
		Object* editorScene = nullptr;
		std::vector<std::vector<std::string>> worldList;
		GameFlow gameFlow;
	} m;
};

template<typename T>
auto World::New(const std::optional<std::string>& name) -> T* {
	auto* world = Instance();
	Object* obj = world->m.children._CreateObject<T>(std::nullopt);
	obj->m_name = name.value_or(std::format("{}_{}", T::static_type(), obj->id()));

	obj->m_parent = nullptr;
	obj->m_scene = nullptr;
	obj->children.parent(obj);
	obj->children.scene(nullptr);

	// Run load and init
	obj->_Init();

	// Schedule the Begin
	ScheduleBegin(obj);

	if (obj->base_type() == SceneT) {
		auto* e = new SceneLoadedEvent { obj->id(), obj->name() };
		event::Send(e);
	}

	return obj;
}

}
