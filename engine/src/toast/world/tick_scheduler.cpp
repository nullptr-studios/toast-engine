#include "tick_scheduler.hpp"

#include <functional>
#include <future>
#include <queue>
#include <stack>
#include <toast/log.hpp>
#include <toast/thread_pool.hpp>
#include <unordered_set>

namespace toast {

using namespace _detail;

#pragma region NODE_CLUSTER

namespace _detail {
void NodeCluster::earlyTick() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::early_tick);
	}
}

void NodeCluster::tick() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::tick);
	}
}

void NodeCluster::postPhysics() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::post_physics);
	}
}

void NodeCluster::lateTick() {
	for (auto& node : nodes) {
		node->callTick(node->info(), TickFunctionList::late_tick);
	}
}

auto NodeCluster::hasEarlyTick() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::early_tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasTick() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasPostPhysics() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::post_physics)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasLateTick() -> bool {
	for (auto& node : nodes) {
		if (node->hasTickFunction(TickFunctionList::late_tick)) {
			return true;
		}
	}
	return false;
}
}

#pragma endregion NODE_CLUSTER

void TickScheduler::registerDependency(Node& from, Node& to) {
	if (&from == &to) {
		TOAST_WARN("World", "{} ({}) tried to register a dependency to itself", from.name(), from.uid());
		return;
	}

	// don't store duplicates
	auto& edges = graph.connections[from];
	if (std::ranges::contains(edges, Box<Node>(to))) {
		return;
	}

	edges.emplace_back(to);
	graph.inverse_connections[to].emplace_back(from);
	TOAST_TRACE("World", "Added dependency from {} to {}", from.name(), from.uid());
}

void TickScheduler::unregisterDependency(Node& from, Node& to) {
	// Remove the dependency from the forward graph
	auto& edges = graph.connections[from];
	std::erase(edges, Box<Node>(to));

	// Remove the dependency from the inverse graph
	auto& inverse_edges = graph.inverse_connections[to];
	std::erase(inverse_edges, Box<Node>(from));
}

void TickScheduler::compute(const std::vector<Box<Node>>& all_nodes) {
	ZoneScoped;
	TOAST_TRACE("World", "Computing dependency graph");

	// Guarantee every existing node exists in the subgraph
	for (const auto& node : all_nodes) {
		graph.connections[node];
		graph.inverse_connections[node];
	}

	auto subgraphs = subgraphSeparation(all_nodes);

	// We are gonna remove nodes that will not be able to get ticked because:
	//		1) they don't have any tick functions
	//		2) they are not in an active state
	for (auto& g : subgraphs) {
		std::erase_if(g, [](const auto& n) { return not n->hasTickFunction(TickFunctionList::tick_mask); });

		std::erase_if(g, [](const auto& n) { return n->m_state != NodeState::root && n->m_state != NodeState::global; });
	}

	// Drop subgraphs that became entirely empty
	std::erase_if(subgraphs, [](const auto& g) { return g.empty(); });

	auto result = tarjanAlgorithm(subgraphs);

	auto waves = assignWaves(result);
	auto ts = optimizeWaves(waves);
	schedule = std::move(ts);
	TOAST_TRACE(
	    "World",
	    "Dependency graph: early={} tick={} post_physics={} late={} waves",
	    schedule.early_tick.size(),
	    schedule.tick.size(),
	    schedule.post_physics.size(),
	    schedule.late_tick.size()
	);
}

void TickScheduler::runPhase(const std::vector<TickSchedule::Wave>& phase, TickFunctionList func, std::string_view name) const {
	ZoneScopedN("TickScheduler::runPhase");    // NOLINT
	ZoneNameF("TickScheduler::runPhase(%s)", name.data());

	for (const auto& wave : phase) {
		std::vector<std::future<void>> futures;
		futures.reserve(wave.size());

		int count = 1;
		for (const auto& n : wave) {
			ZoneScopedN("TickScheduler::runPhase::wave");    // NOLINT
			ZoneNameF("Wave #%i", count++);

			futures.emplace_back(ThreadPool::push([n, func] {
				if (std::holds_alternative<Box<Node>>(n)) {
					auto node = std::get<Box<Node>>(n);
					node->callTick(node->info(), func);
					return;
				}

				// Clusters tick their nodes synchronously to avoid race conditions
				auto cluster = std::get<NodeCluster>(n);
				for (auto& node : cluster.nodes) {
					node->callTick(node->info(), func);
				}
			}));
		}

		{
			ZoneScopedN("Thread Pool semaphore");    // NOLINT
			for (auto& f : futures) {
				f.get();
			}
		}
	}
}

void TickScheduler::run() const {
	ZoneScoped;

	runPhase(schedule.early_tick, TickFunctionList::early_tick, "early_tick");
	runPhase(schedule.tick, TickFunctionList::tick, "tick");
	// TODO: physics step goes between tick and post_physics
	runPhase(schedule.post_physics, TickFunctionList::post_physics, "post_physics");
	runPhase(schedule.late_tick, TickFunctionList::late_tick, "late_tick");
}

auto TickScheduler::subgraphSeparation(const std::vector<Box<Node>>& all_nodes) -> std::vector<std::vector<Box<Node>>> {
	ZoneScoped;

	using NodeGraph = std::vector<Box<Node>>;
	std::unordered_set<Box<Node>> visited;
	std::vector<NodeGraph> subgraphs;

	for (const auto& start_node : all_nodes) {
		if (visited.contains(start_node)) {
			continue;
		}

		ZoneScoped;

		NodeGraph component;
		std::queue<Box<Node>> queue;

		queue.push(start_node);
		visited.insert(start_node);

		while (!queue.empty()) {
			ZoneScoped;

			auto current = queue.front();
			queue.pop();
			component.push_back(current);

			// Walk forward edges
			if (auto it = graph.connections.find(current); it != graph.connections.end()) {
				for (const auto& neighbor : it->second) {
					if (visited.contains(neighbor)) {
						continue;
					}
					visited.insert(neighbor);
					queue.push(neighbor);
				}
			}

			// Walk backward edges
			if (auto it = graph.inverse_connections.find(current); it != graph.inverse_connections.end()) {
				for (const auto& neighbor : it->second) {
					if (visited.contains(neighbor)) {
						continue;
					}
					visited.insert(neighbor);
					queue.push(neighbor);
				}
			}
		}

		NodeGraph subgraph;
		subgraph.reserve(component.size());
		subgraph.assign(std::make_move_iterator(component.begin()), std::make_move_iterator(component.end()));
		subgraphs.push_back(std::move(subgraph));
	}

	return subgraphs;
}

/**
 * produces SCCs in reverse topological order so assignWaves iterates in the right direction;
 * SCCs with more than one node indicate a dependency cycle and are bundled into a NodeCluster
 */
auto TickScheduler::tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& input_subgraphs)
    -> std::vector<TickSchedule::Wave> {
	ZoneScoped;

	// SearchContext tracks the state of the DFS traversal for Tarjan's algorithm.
	struct SearchContext {
		std::unordered_map<Box<Node>, int> index;
		std::unordered_map<Box<Node>, int> low_link;
		std::unordered_map<Box<Node>, bool> on_stack;
		std::stack<Box<Node>> stack;
		int counter = 0;
		std::vector<std::vector<Box<Node>>> sccs;    // Components in reverse topological order
	};

	                                               // clang-format off
	// We start with a list of nodes per subgraph that survived the initial pruning
	std::vector<TickSchedule::Wave> processed_subgraphs;
	processed_subgraphs.reserve(input_subgraphs.size());
	for (const auto& subgraph_nodes : input_subgraphs) {
		TickSchedule::Wave wave;
		wave.reserve(subgraph_nodes.size());
		for (const auto& node : subgraph_nodes) {
			wave.emplace_back(node);
		}
		processed_subgraphs.emplace_back(std::move(wave));
	}
	// clang-format on

	std::vector<TickSchedule::Wave> result;

	for (const auto& current_subgraph : processed_subgraphs) {
		ZoneScoped;

		// Create a lookup set to efficiently filter out neighbors that aren't part of this subgraph
		std::unordered_set<Box<Node>> nodes_in_subgraph;
		for (const auto& variant : current_subgraph) {
			nodes_in_subgraph.insert(std::get<Box<Node>>(variant));
		}

		SearchContext search_context;

		// strong_connect is a recursive DFS function that finds SCCs
		// An SCC is a group of nodes where every node is reachable from every other node in the group
		// In our dependency graph, an SCC with >1 node indicates a circular dependency
		std::function<void(const Box<Node>&)> strong_connect = [&](const Box<Node>& current_node) {
			ZoneScopedN("strong_connect");    // NOLINT

			search_context.index[current_node] = search_context.low_link[current_node] = search_context.counter++;
			search_context.stack.push(current_node);
			search_context.on_stack[current_node] = true;

			// Explore neighbors
			if (auto it = graph.connections.find(current_node); it != graph.connections.end()) {
				for (const auto& neighbor_node : it->second) {
					// We only care about dependencies between nodes that are actually being ticked
					if (!nodes_in_subgraph.contains(neighbor_node)) {
						continue;
					}

					if (!search_context.index.contains(neighbor_node)) {
						// Neighbor hasn't been visited yet; recurse
						strong_connect(neighbor_node);
						search_context.low_link[current_node] =
						    std::min(search_context.low_link[current_node], search_context.low_link[neighbor_node]);
					} else if (search_context.on_stack[neighbor_node]) {
						// Neighbor is on the stack, meaning we've found a cycle
						search_context.low_link[current_node] =
						    std::min(search_context.low_link[current_node], search_context.index[neighbor_node]);
					}
				}
			}

			// If current_node is the root of an SCC, pop the stack to extract the full component
			if (search_context.low_link[current_node] == search_context.index[current_node]) {
				std::vector<Box<Node>> scc;
				while (true) {
					auto popped_node = search_context.stack.top();
					search_context.stack.pop();
					search_context.on_stack[popped_node] = false;
					scc.push_back(popped_node);
					if (popped_node == current_node) {
						break;
					}
				}
				search_context.sccs.emplace_back(std::move(scc));
			}
		};

		for (const auto& variant : current_subgraph) {
			const auto& node = std::get<Box<Node>>(variant);
			if (!search_context.index.contains(node)) {
				strong_connect(node);
			}
		}

		// Convert SCCs into a wave
		TickSchedule::Wave sorted_wave;
		sorted_wave.reserve(search_context.sccs.size());

		for (auto& scc : search_context.sccs) {
			if (scc.size() == 1) {
				sorted_wave.emplace_back(scc[0]);
			} else {
				// Group all nodes in the cycle into a NodeCluster
				// This ensures they are ticked as a single atomic unit to avoid race conditions
				TOAST_TRACE("World", "Dependency cycle detected ({} nodes) -> grouping into NodeCluster", scc.size());
				sorted_wave.emplace_back(NodeCluster(scc));
			}
		}
		result.emplace_back(std::move(sorted_wave));
	}

	return result;
}

/**
 * assigns each item a wave index equal to max(predecessor wave) + 1;
 * items with no predecessors land on wave 0 and run fully in parallel
 */
auto TickScheduler::assignWaves(const std::vector<TickSchedule::Wave>& subgraphs) -> std::vector<TickSchedule::Wave> {
	// First, map every node back to its containing item
	// This allows us to treat clusters as a single scheduling unit
	struct ItemLocation {
		int subgraph_idx;
		int item_idx;
	};

	ZoneScoped;

	std::unordered_map<Box<Node>, ItemLocation> node_to_location;

	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		for (int item_idx = 0; std::cmp_less(item_idx, subgraphs[subgraph_idx].size()); ++item_idx) {
			std::visit(
			    [&](const auto& item) {
				    using T = std::decay_t<decltype(item)>;
				    if constexpr (std::is_same_v<T, Box<Node>>) {
					    node_to_location[item] = {subgraph_idx, item_idx};
				    } else {
					    // NodeCluster
					    for (const auto& node : item.nodes) {
						    node_to_location[node] = {subgraph_idx, item_idx};
					    }
				    }
			    },
			    subgraphs[subgraph_idx][item_idx]
			);
		}
	}

	// Prepare wave levels for every item in every subgraph
	std::vector<std::vector<int>> item_wave_levels(subgraphs.size());
	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		item_wave_levels[subgraph_idx].assign(subgraphs[subgraph_idx].size(), 0);
	}

	// Calculate the wave level for each item
	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		ZoneScopedN("Calculate wave level");

		// Iterating in reverse because Tarjan's SCCs are in reverse topological order
		for (int item_idx = (int)subgraphs[subgraph_idx].size() - 1; item_idx >= 0; --item_idx) {
			// Collect all physical nodes in this scheduling item
			std::vector<Box<Node>> item_nodes;
			std::visit(
			    [&](const auto& item) {
				    using T = std::decay_t<decltype(item)>;
				    if constexpr (std::is_same_v<T, Box<Node>>) {
					    item_nodes.push_back(item);
				    } else {
					    item_nodes.insert(item_nodes.end(), item.nodes.begin(), item.nodes.end());
				    }
			    },
			    subgraphs[subgraph_idx][item_idx]
			);

			// A node must be scheduled in a wave strictly higher than all of its predecessors
			int max_predecessor_wave = -1;
			for (const auto& node : item_nodes) {
				auto it = graph.inverse_connections.find(node);
				if (it == graph.inverse_connections.end()) {
					continue;
				}

				for (const auto& predecessor : it->second) {
					auto loc_it = node_to_location.find(predecessor);
					if (loc_it == node_to_location.end()) {
						continue;    // Predecessor was pruned
					}

					auto [pred_subgraph_idx, pred_item_idx] = loc_it->second;
					// We only care about dependencies within the same subgraph island
					if (pred_subgraph_idx == subgraph_idx && pred_item_idx != item_idx) {
						max_predecessor_wave = std::max(max_predecessor_wave, item_wave_levels[pred_subgraph_idx][pred_item_idx]);
					}
				}
			}
			item_wave_levels[subgraph_idx][item_idx] = max_predecessor_wave + 1;
		}
	}

	// Find the global maximum wave index to determine how many execution buckets we need
	int global_max_wave = 0;
	for (const auto& levels : item_wave_levels) {
		for (int level : levels) {
			global_max_wave = std::max(global_max_wave, level);
		}
	}

	// Group items into waves
	std::vector<TickSchedule::Wave> buckets(global_max_wave + 1);

	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		for (int item_idx = 0; std::cmp_less(item_idx, subgraphs[subgraph_idx].size()); ++item_idx) {
			buckets[item_wave_levels[subgraph_idx][item_idx]].emplace_back(subgraphs[subgraph_idx][item_idx]);
		}
	}

	return buckets;
}

/**
 * prunes items that don't implement the relevant tick function for each phase,
 * then bakes the final wave index into Node::m_wave for O(1) lookup at dispatch time
 */
auto TickScheduler::optimizeWaves(const std::vector<TickSchedule::Wave>& waves) -> TickSchedule {
	TickSchedule schedule = {
	  .early_tick = waves,
	  .tick = waves,
	  .post_physics = waves,
	  .late_tick = waves,
	};

	ZoneScoped;

	/**
	 * filter_and_assign_wave removes nodes from a wave if they don't have the relevant
	 * tick function for that phase, and records the wave index into the node's metadata
	 */
	auto filter_and_assign_wave =
	    [](std::vector<TickSchedule::Wave>& phase_waves, int wave_meta_index, auto&& has_function_checker) {
		    // Remove items that don't participate in this phase
		    for (auto& wave : phase_waves) {
			    std::erase_if(wave, [&](std::variant<Box<Node>, NodeCluster>& item) {
				    if (std::holds_alternative<Box<Node>>(item)) {
					    auto node = std::get<Box<Node>>(item);
					    return !has_function_checker(node);
				    }

				    const auto& cluster = std::get<NodeCluster>(item);
				    return !std::ranges::any_of(cluster.nodes, [&](const auto& node) { return has_function_checker(node); });
			    });
		    }

		    // Drop waves that became empty after filtering
		    std::erase_if(phase_waves, [](const auto& wave) { return wave.empty(); });

		    // Assign the resulting wave index to all surviving nodes
		    for (int wave_idx = 0; std::cmp_less(wave_idx, phase_waves.size()); ++wave_idx) {
			    for (auto& item : phase_waves[wave_idx]) {
				    std::visit(
				        [&](auto& value) {
					        using T = std::decay_t<decltype(value)>;
					        if constexpr (std::is_same_v<T, Box<Node>>) {
						        value->m_wave[wave_meta_index] = wave_idx;
					        } else {
						        for (auto node : value.nodes) {
							        node->m_wave[wave_meta_index] = wave_idx;
						        }
					        }
				        },
				        item
				    );
			    }
		    }
	    };

	filter_and_assign_wave(schedule.early_tick, 0, [](auto n) { return n->hasTickFunction(TickFunctionList::early_tick); });
	filter_and_assign_wave(schedule.tick, 1, [](auto n) { return n->hasTickFunction(TickFunctionList::tick); });
	filter_and_assign_wave(schedule.post_physics, 2, [](auto n) { return n->hasTickFunction(TickFunctionList::post_physics); });
	filter_and_assign_wave(schedule.late_tick, 3, [](auto n) { return n->hasTickFunction(TickFunctionList::late_tick); });

	return schedule;
}

}
