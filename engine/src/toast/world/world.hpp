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
#include "world_events.hpp"

#include <compare>
#include <concepts>
#include <future>
#include <iterator>
#include <queue>
#include <stack>
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
 * dependency graph generation.
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
 * TODO: Documentation
 */
struct TickSchedule {
	/// TODO: Documentation
	using Wave = std::vector<std::variant<Box<Node>, NodeCluster>>;
	std::vector<Wave> early_tick;
	std::vector<Wave> tick;
	std::vector<Wave> post_physics;
	std::vector<Wave> late_tick;
};
}

class World {
public:
	World();
	~World() = default;

	/**
	 * Stores a new dependency
	 * @param from Node that will be ticked first
	 * @param to Node that will be ticked last
	 */
	static void registerDependency(Node& from, Node& to);

	/**
	 * @brief Requests to create a new node during runtime
	 */
	static auto requestRuntimeCreation(Node& parent) -> Box<Node>;

	/**
	 * @brief Creates multiple nodes asynchronously
	 */
	static void dispatchNodeCreation(int count);

	[[nodiscard]]
	auto dependencyGraphGraphviz() const -> std::string;

private:
	inline static World* instance = nullptr;

	/// Creates a node and stores it in memory
	auto nodeAllocation() noexcept -> Box<Node>;

	/// Recalculates the dependency graph and updates the tick_schedule
	void computeDependencyGraph();

	/// Using a BFS flooding algorithm, separates the dependency graph into multiple independent subgraphs
	auto subgraphSeparation() -> std::vector<std::vector<Box<Node>>>;

	/// We use the tarjan algorithm to connect SSCs into clusters so that we don't have circular dependencies
	auto tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& sg) -> std::vector<_detail::TickSchedule::Wave>;

	/// Uses a wave algorithm to assign to each node the order it should be ticked in
	auto assignWaves(const std::vector<_detail::TickSchedule::Wave>& subgraphs) -> std::vector<_detail::TickSchedule::Wave>;

	/// We create a copy of each wave list for each tick function for further node discarding
	auto optimizeWaves(const std::vector<_detail::TickSchedule::Wave>& waves) -> _detail::TickSchedule;

	struct {
		event::Listener listener;
		std::thread load_thread;
		std::unordered_set<_detail::ControlBox> nodes;
	} m;

	struct {
		Box<Node> root;
		std::vector<Box<Node>> global;
		std::vector<Box<Node>> cached;
		std::vector<Box<Node>> load_queue;
		std::vector<Box<Node>> destroy_queue;
	} nodes;

	struct DependencyGraph {
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> connections;
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> inverse_connections;
	} dependency_graph;

	_detail::TickSchedule tick_schedule;

	friend struct toast::_detail::WorldTestAccess;
};

}
