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
#include "wave_executor.hpp"

#include <mutex>
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
	void physicsTick();
	void postPhysics();
	void lateTick();

	auto hasEarlyTick() -> bool;
	auto hasTick() -> bool;
	auto hasPhysicsTick() -> bool;
	auto hasPostPhysics() -> bool;
	auto hasLateTick() -> bool;
};

struct TickSchedule {
	using Wave = std::vector<std::variant<Box<Node>, NodeCluster>>;
	std::vector<Wave> early_tick;
	std::vector<Wave> tick;
	std::vector<Wave> physics_tick;
	std::vector<Wave> post_physics;
	std::vector<Wave> late_tick;
};

/// For a node, the nodes whose scripts it calls or is called by, see Node::interactsWith()
using Interactions = std::unordered_map<Box<Node>, std::vector<Box<Node>>>;

/**
 * @brief Splits a wave into the jobs that run it in parallel
 *
 * Everything that runs Lua on the same interpreter ends up in one job
 *
 * A cluster that spans interpreters ties them together into one job
 */
TOAST_API auto planWave(const TickSchedule::Wave& wave, TickFunctionList func, const Interactions* interactions = nullptr)
    -> std::vector<WaveJob>;
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
	auto registerDependency(Node& from, Node& to) -> bool;
	auto unregisterDependency(Node& from, Node& to) -> bool;
	// clang-format on

	/**
	 * @brief Records that the scripts of two nodes call each other
	 */
	auto registerInteraction(Node& first, Node& second) -> bool;

	/**
	 * @brief Rebuilds the tick schedule from the given node set
	 */
	void compute(const std::vector<Box<Node>>& all_nodes);

	/// Runs the four frame phases (early_tick → tick → post_physics → late_tick) of the schedule
	void run() const;

	/**
	 * @brief Runs a lifecycle stage on `root` and every node below it, parents before their children
	 * @param stage init, begin or on_enable
	 */
	void runLifecycle(Node& root, TickFunctionList stage) const;

	/// Runs a phase wave by wave on the calling thread; for phases that touch thread-bound state like the physics simulator
	void runPhaseSerial(const std::vector<_detail::TickSchedule::Wave>& phase, TickFunctionList func, std::string_view name) const;

	/// Dispatches a single phase of the tick schedule
	void runPhase(const std::vector<_detail::TickSchedule::Wave>& phase, TickFunctionList func, std::string_view name) const;

	/// Scripts register dependencies from init()
	mutable std::mutex graph_mutex;
	DependencyGraph graph;
	_detail::TickSchedule schedule;

	/// The nodes that declared an interaction
	_detail::Interactions interactions;

private:
	/// Ticks one item of a wave
	static void tickItem(const _detail::TickSchedule::Wave::value_type& item, TickFunctionList func);

	/// Runs the jobs of a wave
	static void runWave(
	    const _detail::TickSchedule::Wave& wave, const std::vector<_detail::WaveJob>& jobs, TickFunctionList func, int number
	);

	struct CachedPlan {
		TickFunctionList func = TickFunctionList::none;
		std::vector<_detail::WaveJob> jobs;
	};

	auto planFor(const _detail::TickSchedule::Wave& wave, TickFunctionList func) const -> const std::vector<_detail::WaveJob>&;

	mutable std::unordered_map<const _detail::TickSchedule::Wave*, CachedPlan> m_plans;

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
