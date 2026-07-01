/**
 * @file World.hpp
 * @author Xein
 * @date 18 May 2026
 *
 * @brief This class creates, owns, ticks and manages every Node that ever exists in the engine
 */

#pragma once
#include "box.hpp"
#include "control_box.hpp"
#include "node.hpp"
#include "node_owner.hpp"
#include "reflect.hpp"
#include "world_events.hpp"

#include <compare>
#include <concepts>
#include <future>
#include <iterator>
#include <mutex>
#include <queue>
#include <stack>
#include <toast/assets/prefab.hpp>
#include <toast/events/listener.hpp>
#include <toast/log.hpp>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace toast {

class Node;

namespace _detail {
/**
 * @struct NodeCluster
 *
 * This class combines multiples nodes into one single cluster. It is used for
 * grouping strongly connected components into one single component for the
 * dependency graph generation
 *
 * The nodes are ticked in the order they are on the vector, but in most cases
 * this shouldn't be an issue, as the main concern is to avoid race conditions
 * when ticking
 */
struct NodeCluster {
	explicit NodeCluster(std::vector<Box<Node>>& nodes) : nodes(std::move(nodes)) { }

	std::vector<Box<Node>> nodes;

	// This functions call all the ticks of the nodes synchronously
	void earlyTick();
	void tick();
	void postPhysics();
	void lateTick();

	// This functions return true if there's at least one function of that type
	auto hasEarlyTick() -> bool;
	auto hasTick() -> bool;
	auto hasPostPhysics() -> bool;
	auto hasLateTick() -> bool;
};

/**
 * @struct TickSchedule
 *
 * Compiled execution plan produced by World::computeDependencyGraph().
 * A Wave is a batch of items that can run in parallel; each item is either
 * a single Box<Node> or a NodeCluster grouping a dependency cycle (SCC).
 */
struct TickSchedule {
	using Wave = std::vector<std::variant<Box<Node>, NodeCluster>>;
	std::vector<Wave> early_tick;      ///< waves for the earlyTick phase
	std::vector<Wave> tick;            ///< waves for the tick phase
	std::vector<Wave> post_physics;    ///< waves for the postPhysics phase
	std::vector<Wave> late_tick;       ///< waves for the lateTick phase
};
}

class World final : public INodeOwner {
public:
	World();
	~World() override;

	auto name() -> std::string override { return "World"; }

	void tick() override;

	/**
	 * @brief Records a tick ordering constraint between two active nodes
	 * @param from Node that must be ticked before `to`
	 * @param to Node that must be ticked after `from`
	 * @note Triggers a full rebuild of the dependency graph at the start of the next tick
	 */
	void registerDependency(Node& from, Node& to) override;

	/**
	 * @brief Begins asynchronous loading of a prefab into the cache
	 * @param uid UID of the prefab asset to load
	 * @note The node appears in trees.cached at the start of the next tick() after loading finishes;
	 *       call setRoot() afterwards to make it the active scene
	 */
	static void loadNode(UID uid);

	/**
	 * @brief Begins asynchronous loading of a prefab into the cache
	 * @param uri Virtual URI of the prefab, e.g. "assets://levels/lobby.node"
	 * @note Resolves the URI to a UID via the manifest, then delegates to loadNode(UID)
	 */
	static void loadNode(std::string_view uri);

	auto findFrom(const Node& origin, std::string_view query) -> Box<Node> override;
	auto searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> override;

	/**
	 * @brief Promotes a cached or global node to be the active world root
	 *
	 * Calls end() on the old root and begin() on the new one so there is never a frame
	 * with two simultaneous active roots.
	 *
	 * @param node Must be in the cached or global state with NodeType::root
	 * @return The previous world root, now cached, or an empty box if the world was empty
	 * @warning Passing an already-rooted node is a no-op; a warning is logged
	 */
	static auto setRoot(Node& node) -> Box<Node>;

	/**
	 * @brief Moves an active node back to the cached state, disabling it and calling its end() lifecycle
	 * @param node Any node in the root or global state
	 * @return The node's own box
	 * @note Caching the active world root leaves the world without a root until setRoot() is called again
	 */
	static auto cacheNode(Node& node) -> Box<Node>;

	/**
	 * @brief Schedules a cached node for destruction at the start of the next tick
	 * @param node Must be in the cached state; active nodes must be cached first
	 * @note Code holding a Box<Node> to the node remains safe until the queue is drained next frame
	 * @warning Only cached nodes can be destroyed; attempting to destroy an active node logs a warning and does nothing
	 */
	static void destroyNode(Node& node);

	/**
	 * @brief Linear search for a cached root node by display name
	 * @param name The node's display name to search for
	 * @return The first cached node with a matching name, or an empty box if not found
	 * @note Names are not unique; returns the first match
	 */
	[[nodiscard]]
	static auto findCached(std::string_view name) -> Box<Node>;

	/**
	 * @brief Serializes the current tick schedule as a Graphviz DOT string
	 * @return A DOT-format string visualizing nodes, waves, and dependency edges per phase;
	 *         pass to the graphviz/dot tool to render
	 */
	[[nodiscard]]
	static auto graphviz() -> std::string;

	/**
	 * @brief Invalidates the world transforms of all Node3D nodes that depend on the given node
	 * @param node The node whose transform changed
	 * @note Called by Node3D setters; only nodes listed in inverse_connections are dirtied
	 */
	static void markNode3DDependantsDirty(const Box<Node>& node) noexcept;

	[[nodiscard]]
	auto dependencyGraphGraphviz() const -> std::string;

private:
	inline static World* instance = nullptr;

	/// Rebuilds the dependency graph from the current node set and recomputes the tick schedule
	void computeDependencyGraph();

	/// BFS flood-fill that partitions the dependency graph into independent subgraphs with no shared edges
	auto subgraphSeparation() -> std::vector<std::vector<Box<Node>>>;

	/// Tarjan SCC; bundles cycles into NodeClusters and returns items in reverse topological order
	auto tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& input_subgraphs) -> std::vector<_detail::TickSchedule::Wave>;

	/// Assigns each item wave = max(predecessor wave) + 1; items with no predecessors land on wave 0
	auto assignWaves(const std::vector<_detail::TickSchedule::Wave>& subgraphs) -> std::vector<_detail::TickSchedule::Wave>;

	/// Prunes items that don't implement the relevant tick function per phase; bakes wave indices into Node::m_wave
	auto optimizeWaves(const std::vector<_detail::TickSchedule::Wave>& waves) -> _detail::TickSchedule;

	/// Atomically replaces the world root; the old root is returned as a cached node
	auto swapRoot(Node& node) -> Box<Node>;

	/// Detaches a node from the root or global list, disables it, and calls its end() lifecycle
	auto moveToCached(Node& node) -> Box<Node>;

	/// Promotes a cached root node to the global list; calls begin() and enabled(true)
	auto moveToGlobal(Node& node) -> Box<Node>;

	/// Reparents a node under parent; calls begin() and enabled(true) if it was previously cached
	auto moveToChild(Node& node, Node& parent) -> Box<Node>;

	/// Moves async-loaded nodes from the thread-safe load queue into trees.cached
	void drainLoadQueue();

	/// Runs destroy() callbacks, severs Box<> edges, frees nodes, then reaps tombstones
	void drainDestroyQueue();

	/// Places async-spawned nodes under their target parent; deduplicates UIDs if the same prefab was spawned twice
	void drainSpawnQueue();

	/// Kicks off the async spawn worker that loads a prefab and enqueues it for attachment to parent
	static void spawn(UID prefab, Node& parent);

	/// DFS from scope (or the world root) for a node by UID; stops at prefab-instance boundaries
	[[nodiscard]]
	static auto findNode(const UID& uid, Node* scope = nullptr) -> Box<Node>;

	/// Resolves a slash-separated UID path by walking each segment in turn
	[[nodiscard]]
	static auto findNode(std::string_view path) -> Box<Node>;

	/// Builds the slash-separated UID path from the world root down to node; used by the query parser
	[[nodiscard]]
	static auto uidPath(const Node& node) -> std::string;

	/// Single-segment DFS within one prefab instance scope; does not cross instance boundaries
	[[nodiscard]]
	static auto findScoped(Node& scope, std::string_view seg, bool by_uid) -> Box<Node>;

	/// All-matching DFS that crosses prefab-instance boundaries; appends results to out
	static void searchScoped(Node& scope, std::string_view seg, bool by_uid, std::vector<Box<Node>>& out);

	struct {
		event::Listener listener;
		std::mutex load_mutex;
		std::vector<std::future<void>> load_futures;
		std::vector<std::pair<Box<Node>, Box<Node>>> spawn_queue;
	} m;

	struct Trees {
		Box<Node> root;                          ///< the active world root; only one at a time
		std::vector<Box<Node>> global;           ///< always-active nodes that sit outside the main tree
		std::vector<Box<Node>> cached;           ///< loaded but inactive; waiting to become root or be destroyed
		std::vector<Box<Node>> load_queue;       ///< assets being loaded asynchronously
		std::vector<Box<Node>> destroy_queue;    ///< nodes queued for destruction; drained at the start of tick
	} trees;

	struct DependencyGraph {
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> connections;    ///< forward edges (from → to)
		std::unordered_map<Box<Node>, std::vector<Box<Node>>>
		    inverse_connections;    ///< reverse edges; lets wave assignment find predecessors in O(1)
	} dependency_graph;

	_detail::TickSchedule tick_schedule;

	// for testing only
	friend struct toast::_detail::WorldTestAccess;
};

}
