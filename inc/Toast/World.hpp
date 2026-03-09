#pragma once
#include "Nodes/Node.hpp"
#include "Toast/Event/ListenerSubNode.hpp"
#include "Toast/GameFlow.hpp"
#include "Toast/RootNodeLoadedEvent.hpp"

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
	static auto New(std::string_view type, const std::optional<std::string>& name = std::nullopt) -> Node*;
	static auto LoadRootNode(std::string_view path) -> std::future<unsigned>;    ///< Loads scene on the init thread, scene disabld after load
	static void LoadRootNodeSync(std::string_view path);                         ///< Loads scene on the main thread, scene enabled after load
	static void UnloadRootNode(unsigned id);
	static void UnloadRootNode(std::string_view name);
	static void EnableRootNode(unsigned id);
	static void EnableRootNode(std::string_view name);
	static void DisableRootNode(unsigned id);
	static void DisableRootNode(std::string_view name);

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
	static auto GetFromType() -> T* {
		return static_cast<T*>(Instance()->m.children.GetType(T::static_type(), true));
	}

	[[nodiscard]]
	static Node* Get(unsigned id);
	[[nodiscard]]
	static Node* Get(std::string_view name);
	[[nodiscard]]
	static bool Has(unsigned id);
	[[nodiscard]]
	static bool Has(std::string_view name);
	[[nodiscard]]
	static Node::Children& GetChildren();

	[[nodiscard]]
	static auto GetFromType(std::string_view type) -> Node*;

	static void ScheduleBegin(Node* obj);
	static void CancelBegin(Node* obj);
	static void ScheduleDestroy(Node* obj);
	[[nodiscard]]
	const std::list<Node*>& begin_queue() const;

#ifdef TOAST_EDITOR
	void SetEditorRootNode(Node* obj);
#endif

private:
	void OnSimulateWorld(bool value);

	static World* m_instance;
	constexpr static unsigned char DESTROY_SCENE_DELAY = 10;
	constexpr static size_t POOL_SIZE = 2;

	struct {
		Node::Children children;
		std::unique_ptr<event::ListenerSubNode> listener;
		std::unordered_map<unsigned, RootNode*> tickableRootNodes;
		std::unordered_map<unsigned, unsigned> sceneDestroyTimers;
		ThreadPool* threadPool = nullptr;
		bool simulateWorld = true;
		std::map<unsigned, std::string> loadedRootNodes;
		std::map<unsigned, bool> loadedRootNodesStatus;
		std::list<Node*> beginQueue;
		std::list<Node*> destroyQueue;
		std::mutex queueMutex;
		Node* editorRootNode = nullptr;
		std::vector<std::vector<std::string>> worldList;
		GameFlow gameFlow;
	} m;
};

template<typename T>
auto World::New(const std::optional<std::string>& name) -> T* {
	auto* world = Instance();
	Node* obj = world->m.children._CreateNode<T>(std::nullopt);
	obj->m_name = name.value_or(std::format("{}_{}", T::static_type(), obj->id()));

	obj->m_parent = nullptr;
	obj->m_scene = nullptr;
	obj->children.parent(obj);
	obj->children.scene(nullptr);

	// Run load and init
	obj->_Init();

	// Schedule the Begin
	ScheduleBegin(obj);

	if (obj->base_type() == RootNodeT) {
		auto* e = new RootNodeLoadedEvent { obj->id(), obj->name() };
		event::Send(e);
	}

	return obj;
}

}
