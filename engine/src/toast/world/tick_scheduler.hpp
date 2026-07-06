/**
 * @file tick_scheduler.hpp
 * @author Xein
 * @date 06 Jul 2026
 *
 * @brief Dependency-graph tick scheduling shared by World and PlayWorkspace
 */

#pragma once
#include "box.hpp"
#include "node.hpp"

#include <unordered_map>
#include <variant>
#include <vector>

namespace toast {

namespace _detail {
/**
 * @struct NodeCluster
 *
 * This class combines multiples nodes into one single cluster
 *
 * It is used for grouping SCCs into one single component for the
 * dependency graph generation
 *
 * The nodes are ticked in the order they are on the vector, but in most cases
 * this shouldn't be an issue
 */
struct NodeCluster {
	explicit NodeCluster(std::vector<Box<Node>>& nodes) : nodes(std::move(nodes)) { }

	std::vector<Box<Node>> nodes;

	void earlyTick();
	void tick();
	void postPhysics();
	void lateTick();

	auto hasEarlyTick() -> bool;
	auto hasTick() -> bool;
	auto hasPostPhysics() -> bool;
	auto hasLateTick() -> bool;
};

struct TickSchedule {
	using Wave = std::vector<std::variant<Box<Node>, NodeCluster>>;
	std::vector<Wave> early_tick;
	std::vector<Wave> tick;
	std::vector<Wave> post_physics;
	std::vector<Wave> late_tick;
};
}

class TickScheduler {
public:
	struct DependencyGraph {
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> connections;            ///< forward edges (from → to)
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> inverse_connections;    ///< reverse edges
	};

	                                                                                // clang-format off
  /**
   * @brief Records a tick ordering constraint between two nodes
   * @note The schedule is NOT rebuilt automatically; the owner calls compute() when appropriate
   */
	void registerDependency(Node& from, Node& to);
	void unregisterDependency(Node& from, Node& to);
	// clang-format on

	/**
	 * @brief Rebuilds the tick schedule from the given node set
	 */
	void compute(const std::vector<Box<Node>>& all_nodes);

	/// Runs all four phases (early_tick → tick → post_physics → late_tick) of the schedule
	void run() const;

	DependencyGraph graph;
	_detail::TickSchedule schedule;

private:
	/// BFS flood-fill that partitions the dependency graph into independent subgraphs with no shared edges
	auto subgraphSeparation(const std::vector<Box<Node>>& all_nodes) -> std::vector<std::vector<Box<Node>>>;

	/// Tarjan SCC; bundles cycles into NodeClusters and returns items in reverse topological order
	auto tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& input_subgraphs) -> std::vector<_detail::TickSchedule::Wave>;

	/// Assigns each item wave = max(predecessor wave) + 1; items with no predecessors land on wave 0
	auto assignWaves(const std::vector<_detail::TickSchedule::Wave>& subgraphs) -> std::vector<_detail::TickSchedule::Wave>;

	/// Prunes items that don't implement the relevant tick function per phase
	auto optimizeWaves(const std::vector<_detail::TickSchedule::Wave>& waves) -> _detail::TickSchedule;
};

}
