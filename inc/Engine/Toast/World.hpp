#pragma once
#include "Objects/Object.hpp"

#include <Engine/Core/ThreadPool.hpp>
#include <Engine/Event/ListenerComponent.hpp>
#include <Engine/Toast/SceneLoadedEvent.hpp>
#include <mutex>

namespace toast {

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
	static unsigned
	    New(const std::optional<std::string_view>& name = std::nullopt, const std::optional<json_t>& j = std::nullopt,
	        const std::function<void(Scene*)>& init_callback = {});
	static unsigned
	    New(const std::string& type, const std::optional<std::string>& name = std::nullopt, const std::optional<json_t>& j = std::nullopt,
	        const std::function<void(Scene*)>& init_callback = {});
	static void LoadScene(std::string_view path, std::optional<std::function<void(Scene*)>>&& load_callback = {});
	static void UnloadScene(unsigned id);
	static void UnloadScene(const std::string& name);
	static void EnableScene(unsigned id);
	static void EnableScene(const std::string& name);
	static void DisableScene(unsigned id);
	static void DisableScene(const std::string& name);

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
	static T* Get(const std::string& name) {
		return static_cast<T*>(Get(name));
	}

	[[nodiscard]]
	static Object* Get(unsigned id);
	[[nodiscard]]
	static Object* Get(const std::string& name);
	[[nodiscard]]
	static bool Has(unsigned id);
	[[nodiscard]]
	static bool Has(const std::string& name);
	[[nodiscard]]
	static Object::Children& GetChildren();

	static void ScheduleBegin(Object* obj);
	static void CancelBegin(Object* obj);
	static void ScheduleDestroy(Object* obj);
	[[nodiscard]]
	const std::list<Object*>& begin_queue() const;

#ifdef TOAST_EDITOR
	void SetEditorScene(Object* obj);
#endif

private:
	static World* m_instance;
	Object::Children m_children;
	std::unique_ptr<event::ListenerComponent> m_listener;

	std::unordered_map<unsigned, Scene*> m_tickableScenes {};
	std::unordered_map<unsigned, unsigned> m_sceneDestroyTimers {};
	constexpr static unsigned char DESTROY_SCENE_DELAY = 10;
	constexpr static size_t POOL_SIZE = 2;
	ThreadPool* m_threadPool = nullptr;

	bool m_simulateWorld = true;
	std::map<unsigned, std::string> m_loadedScenes;
	std::map<unsigned, bool> m_loadedScenesStatus;

	std::list<Object*> m_beginQueue {};
	std::list<Object*> m_destroyQueue {};
	std::mutex m_queueMutex {};

#ifdef TOAST_EDITOR
	Object* m_editorScene = nullptr;
	void OnSimulateWorld(bool value);
#endif
};

template<typename T>
unsigned World::New(const std::optional<std::string_view>& name, const std::optional<json_t>& j, const std::function<void(Scene*)>& init_callback) {
	unsigned obj_id = Factory::AssignId();

	Instance()->m_threadPool->QueueJob([name, j, init_callback, obj_id]<T> {
		auto* world = Instance();
		std::string obj_name {};
		{
			auto obj = world->m_children._CreateObject<T>();
			// Yes, this needed to be done
			obj->m_id = obj_id;
			if (std::is_base_of_v<T, Scene>()) {
				world->m_tickableScenes[obj_id] = static_cast<Scene*>(obj.get());
			}

			// Add name to the scene
			if (name.has_value()) {
				obj->name(*name);
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
		}

		if (std::is_base_of_v<T, Scene>()) {
			auto* e = new SceneLoadedEvent { obj_id, obj_name };
			if (init_callback) {
				e->loadCallback = init_callback;
			}
			event::Send(new SceneLoadedEvent { obj_id, obj_name });
		}
	});

	return obj_id;
}

}
