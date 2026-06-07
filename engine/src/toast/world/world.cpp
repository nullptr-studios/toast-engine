#include "world.hpp"

#include "world_test_access.hpp"
#include "node_3d.hpp"

#include <sstream>
#include <toast/thread_pool.hpp>

namespace toast {

#pragma region NODE_CLUSTER

namespace _detail {
void NodeCluster::earlyTick() {
	for (auto& node : nodes) {
		node->table->earlyTick(*node);
	}
}

void NodeCluster::tick() {
	for (auto& node : nodes) {
		node->table->tick(*node);
	}
}

void NodeCluster::postPhysics() {
	for (auto& node : nodes) {
		node->table->postPhysics(*node);
	}
}

void NodeCluster::lateTick() {
	for (auto& node : nodes) {
		node->table->lateTick(*node);
	}
}

auto NodeCluster::hasEarlyTick() -> bool {
	for (auto& node : nodes) {
		if (not node->table->table.early_tick.empty()) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasTick() -> bool {
	for (auto& node : nodes) {
		if (not node->table->table.tick.empty()) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasPostPhysics() -> bool {
	for (auto& node : nodes) {
		if (not node->table->table.post_physics.empty()) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasLateTick() -> bool {
	for (auto& node : nodes) {
		if (not node->table->table.late_tick.empty()) {
			return true;
		}
	}
	return false;
}
}

#pragma endregion NODE_CLUSTER

World::World() {
	instance = this;
}

auto World::nodeAllocation() noexcept -> Box<Node> {
	ZoneScoped;
	auto [it, result] = m.nodes.emplace(new Node());
	TOAST_ASSERT(result, "World", "Node allocation failed");
	return it->node;
}

void World::registerDependency(Node& from, Node& to) {
	if (&from == &to) {
		TOAST_WARN("World", "{} ({}) tried to register a dependency to itself", from.name(), from.uid());
		return;
	}
	// TODO: sanity check
	// auto& node_set = instance->m.nodes;
	// if (node_set.find(from.box()) == node_set.end()) {
	//
	// }

	instance->dependency_graph.connections[from].emplace_back(to);
	instance->dependency_graph.inverse_connections[to].emplace_back(from);
}

auto World::requestRuntimeCreation(Node& parent) -> Box<Node> {
	ZoneScoped;

	// Phase 1: allocation
	Box node = instance->nodeAllocation();
	node->table->load(node);
	node->table->preInit(node);

	// Phase 2: data structure generation
	node->m.uid.generate();
	node->m.parent = parent;
	parent.m.children.emplace_back(node);
	// TODO: dependency graph

	// Phase 3: node initialization
	node->table->init(node);
	return node;
}

void World::dispatchNodeCreation(int count) {
	ZoneScoped;

	// Phase 1: allocation
	std::vector<std::future<Box<Node>>> alloc_futures;
	alloc_futures.reserve(count);
	for (int i = 0; i < count; i++) {
		alloc_futures.emplace_back(ThreadPool::push([]() {
			Box node = instance->nodeAllocation();
			node->table->load(node);
			node->table->preInit(node);
			node->m.state = NodeState::loading;
			return node;
		}));
	}

	for (auto& r : alloc_futures) {
		r.wait();
	}

	// Phase 2: data structure generation
	for (auto& r : alloc_futures) {
		r.get()->m.uid.generate();
	}
	// TODO: build tree
	// TODO: build dependency graph

	// Phase 3: node initialization
	// TODO: placeholder
	std::vector<std::future<Box<Node>>> init_futures;
	init_futures.reserve(count);
	for (int i = 0; i < count; i++) {
		init_futures.emplace_back(ThreadPool::push([node = alloc_futures[i].get()]() mutable {
			node->table->init(node);
			return node;
		}));
	}

	for (auto& r : init_futures) {
		r.wait();
		r.get()->m.state = NodeState::cached;
	}

	// TODO: Move this to cached_list
}

void World::markNode3DDependantsDirty(const Box<Node>& node) noexcept {
	if (!instance) return;

	auto it = instance->dependency_graph.inverse_connections.find(node);
	if (it != instance->dependency_graph.inverse_connections.end()) {
		for (auto& dependent : it->second) {
			if (auto node3d = dependent.as<Node3D>()) {
				node3d->m_dirty_world = true;
			}
		}
	}
}
void World::computeDependencyGraph() {
	// Guarantee every existing node exists in the subgraph
	for (const auto& node : m.nodes) {
		dependency_graph.connections[node.node->box()];
		dependency_graph.inverse_connections[node.node->box()];
	}

	auto subgraphs = subgraphSeparation();

	// We are gonna remove nodes that will not be able to get ticked because:
	//		1) they don't have any tick functions
	//		2) they are in the NodeState::cached
	for (auto& g : subgraphs) {
		std::erase_if(g, [](const auto& n) {
			return n->table->table.early_tick.empty() && n->table->table.tick.empty() && n->table->table.post_physics.empty() &&
			       n->table->table.late_tick.empty();
		});

		std::erase_if(g, [](const auto& n) { return n->m.state == NodeState::cached; });
	}

	// Drop subgraphs that became entirely empty
	std::erase_if(subgraphs, [](const auto& g) { return g.empty(); });

	auto result = tarjanAlgorithm(subgraphs);

	auto waves = assignWaves(result);
	auto ts = optimizeWaves(waves);
	tick_schedule = std::move(ts);
}

auto World::subgraphSeparation() -> std::vector<std::vector<Box<Node>>> {
	using NodeGraph = std::vector<Box<Node>>;
	std::unordered_set<Box<Node>> visited;
	std::vector<NodeGraph> subgraphs;

	for (const auto& start_cn : m.nodes) {
		auto start_node = start_cn.node->box();
		if (visited.contains(start_node)) {
			continue;
		}

		NodeGraph component;
		std::queue<Box<Node>> queue;

		queue.push(start_node);
		visited.insert(start_node);

		while (!queue.empty()) {
			auto current = queue.front();
			queue.pop();
			component.push_back(current);

			// Walk forward edges
			if (auto it = dependency_graph.connections.find(current); it != dependency_graph.connections.end()) {
				for (const auto& neighbor : it->second) {
					if (visited.contains(neighbor)) {
						continue;
					}
					visited.insert(neighbor);
					queue.push(neighbor);
				}
			}

			// Walk backward edges
			if (auto it = dependency_graph.inverse_connections.find(current); it != dependency_graph.inverse_connections.end()) {
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

auto World::tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& input_subgraphs)
    -> std::vector<_detail::TickSchedule::Wave> {
	// SearchContext tracks the state of the DFS traversal for Tarjan's algorithm.
	struct SearchContext {
		std::unordered_map<Box<Node>, int> index;
		std::unordered_map<Box<Node>, int> low_link;
		std::unordered_map<Box<Node>, bool> on_stack;
		std::stack<Box<Node>> stack;
		int counter = 0;
		std::vector<std::vector<Box<Node>>> sccs;    // Components in reverse topological order
	};

	                                               // We start with a list of nodes per subgraph that survived the initial pruning
	std::vector<_detail::TickSchedule::Wave> processed_subgraphs;
	processed_subgraphs.reserve(input_subgraphs.size());
	for (const auto& subgraph_nodes : input_subgraphs) {
		_detail::TickSchedule::Wave wave;
		wave.reserve(subgraph_nodes.size());
		for (const auto& node : subgraph_nodes) {
			wave.emplace_back(node);
		}
		processed_subgraphs.emplace_back(std::move(wave));
	}

	std::vector<_detail::TickSchedule::Wave> result;

	for (const auto& current_subgraph : processed_subgraphs) {
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
			search_context.index[current_node] = search_context.low_link[current_node] = search_context.counter++;
			search_context.stack.push(current_node);
			search_context.on_stack[current_node] = true;

			// Explore neighbors
			if (auto it = dependency_graph.connections.find(current_node); it != dependency_graph.connections.end()) {
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
						// Neighbor is on the stack, meaning we've found a cyclw
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
		_detail::TickSchedule::Wave sorted_wave;
		sorted_wave.reserve(search_context.sccs.size());

		for (auto& scc : search_context.sccs) {
			if (scc.size() == 1) {
				sorted_wave.emplace_back(scc[0]);
			} else {
				// Group all nodes in the cycle into a NodeCluster
				// This ensures they are ticked as a single atomic unit to avoid race conditions
				TOAST_TRACE("World", "Dependency cycle detected ({} nodes) -> grouping into NodeCluster", scc.size());
				sorted_wave.emplace_back(_detail::NodeCluster(scc));
			}
		}
		result.emplace_back(std::move(sorted_wave));
	}

	return result;
}

auto World::assignWaves(const std::vector<_detail::TickSchedule::Wave>& subgraphs) -> std::vector<_detail::TickSchedule::Wave> {
	// First, map every node back to its containing item
	// This allows us to treat clusters as a single scheduling unit
	struct ItemLocation {
		int subgraph_idx;
		int item_idx;
	};

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
				auto it = dependency_graph.inverse_connections.find(node);
				if (it == dependency_graph.inverse_connections.end()) {
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
	std::vector<_detail::TickSchedule::Wave> buckets(global_max_wave + 1);

	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		for (int item_idx = 0; std::cmp_less(item_idx, subgraphs[subgraph_idx].size()); ++item_idx) {
			buckets[item_wave_levels[subgraph_idx][item_idx]].emplace_back(subgraphs[subgraph_idx][item_idx]);
		}
	}

	return buckets;
}

auto World::optimizeWaves(const std::vector<_detail::TickSchedule::Wave>& waves) -> _detail::TickSchedule {
	_detail::TickSchedule schedule = {
	  .early_tick = waves,
	  .tick = waves,
	  .post_physics = waves,
	  .late_tick = waves,
	};

	/**
	 * filter_and_assign_wave removes nodes from a wave if they don't have the relevant
	 * tick function for that phase, and records the wave index into the node's metadata
	 */
	auto filter_and_assign_wave =
	    [](std::vector<_detail::TickSchedule::Wave>& phase_waves, int wave_meta_index, auto&& has_function_checker) {
		    for (int wave_idx = 0; wave_idx < (int)phase_waves.size(); ++wave_idx) {
			    auto& current_wave = phase_waves[wave_idx];

			    // Remove items that don't participate in this phase
			    std::erase_if(current_wave, [&](std::variant<Box<Node>, _detail::NodeCluster>& item) {
				    if (std::holds_alternative<Box<Node>>(item)) {
					    auto node = std::get<Box<Node>>(item);
					    return !has_function_checker(node);
				    }

				    const auto& cluster = std::get<_detail::NodeCluster>(item);
				    return !std::ranges::any_of(cluster.nodes, [&](const auto& node) { return has_function_checker(node); });
			    });

			    // Assign the resulting wave index to all surviving nodes
			    for (auto& item : current_wave) {
				    std::visit(
				        [&](auto& value) {
					        using T = std::decay_t<decltype(value)>;
					        if constexpr (std::is_same_v<T, Box<Node>>) {
						        value->m.wave[wave_meta_index] = wave_idx;
					        } else {
						        for (auto node : value.nodes) {
							        node->m.wave[wave_meta_index] = wave_idx;
						        }
					        }
				        },
				        item
				    );
			    }
		    }

		    // Drop waves that became empty after filtering
		    std::erase_if(phase_waves, [](const auto& wave) { return wave.empty(); });
	    };

	filter_and_assign_wave(schedule.early_tick, 0, [](auto n) { return !n->table->table.early_tick.empty(); });
	filter_and_assign_wave(schedule.tick, 1, [](auto n) { return !n->table->table.tick.empty(); });
	filter_and_assign_wave(schedule.post_physics, 2, [](auto n) { return !n->table->table.post_physics.empty(); });
	filter_and_assign_wave(schedule.late_tick, 3, [](auto n) { return !n->table->table.late_tick.empty(); });

	return schedule;
}

auto World::dependencyGraphGraphviz() const -> std::string {
	auto escape = [](std::string_view value) {
		std::string result;
		result.reserve(value.size());
		for (char c : value) {
			if (c == '\\' || c == '"') {
				result.push_back('\\');
			}
			result.push_back(c);
		}
		return result;
	};

	auto stage_name = [](std::string_view value) {
		std::string result;
		result.reserve(value.size());
		for (char c : value) {
			result.push_back(c == ' ' ? '_' : c);
		}
		return result;
	};

	std::ostringstream out;
	out << "digraph DependencyGraph {\n";
	out << "  rankdir=LR;\n";
	out << "  node [shape=box];\n";

	auto emit_stage = [&](std::string_view stage_label, const auto& stage_schedule) {
		std::unordered_map<std::size_t, std::string> node_ids;
		std::unordered_map<std::size_t, std::string> node_labels;
		std::unordered_map<std::size_t, Box<Node>> scheduled_nodes;

		auto register_node = [&](const Box<Node>& node) {
			auto key = node.rid();
			auto [id_it, inserted] = node_ids.try_emplace(key, stage_name(stage_label) + "_n" + std::to_string(node_ids.size()));
			if (inserted) {
				node_labels.emplace(key, std::string(node->name()));
				scheduled_nodes.emplace(key, node);
			}
			return id_it->second;
		};

		for (const auto& wave : stage_schedule) {
			for (const auto& item : wave) {
				std::visit(
				    [&](const auto& value) {
					    using T = std::decay_t<decltype(value)>;
					    if constexpr (std::is_same_v<T, Box<Node>>) {
						    register_node(value);
					    } else {
						    for (const auto& node : value.nodes) {
							    register_node(node);
						    }
					    }
				    },
				    item
				);
			}
		}

		out << "  subgraph cluster_" << stage_name(stage_label) << " {\n";
		out << "    label=\"" << escape(stage_label) << "\";\n";
		out << "    color=\"lightgrey\";\n";

		for (const auto& [rid, label] : node_labels) {
			out << "    " << node_ids.at(rid) << " [label=\"" << escape(label) << "\"];\n";
		}

		for (std::size_t wave_index = 0; wave_index < stage_schedule.size(); ++wave_index) {
			const auto& wave = stage_schedule[wave_index];
			out << "    subgraph cluster_" << stage_name(stage_label) << "_wave_" << wave_index << " {\n";
			out << "      label=\"wave " << wave_index << "\";\n";
			out << "      rank=same;\n";

			std::size_t scc_index = 0;
			for (const auto& item : wave) {
				std::visit(
				    [&](const auto& value) {
					    using T = std::decay_t<decltype(value)>;
					    if constexpr (std::is_same_v<T, Box<Node>>) {
						    out << "      " << node_ids.at(value.rid()) << ";\n";
					    } else {
						    auto current_scc_index = scc_index++;
						    out << "      subgraph cluster_" << stage_name(stage_label) << "_wave_" << wave_index << "_scc_"
						        << current_scc_index << " {\n";
						    out << "        label=\"SCC " << current_scc_index << "\";\n";
						    out << "        color=\"gold\";\n";
						    for (const auto& node : value.nodes) {
							    out << "        " << node_ids.at(node.rid()) << ";\n";
						    }
						    out << "      }\n";
					    }
				    },
				    item
				);
			}
			out << "    }\n";
		}

		for (const auto& [rid, node] : scheduled_nodes) {
			std::queue<Box<Node>> q;
			std::unordered_set<std::size_t> visited;
			std::unordered_set<std::size_t> connected;

			if (auto it = dependency_graph.connections.find(node); it != dependency_graph.connections.end()) {
				for (const auto& succ : it->second) {
					q.push(succ);
					visited.insert(succ.rid());
				}
			}

			while (!q.empty()) {
				Box<Node> curr = q.front();
				q.pop();

				auto curr_rid = curr.rid();
				if (scheduled_nodes.contains(curr_rid)) {
					if (connected.insert(curr_rid).second) {
						out << "    " << node_ids.at(rid) << " -> " << node_ids.at(curr_rid) << ";\n";
					}
				} else {
					if (auto it = dependency_graph.connections.find(curr); it != dependency_graph.connections.end()) {
						for (const auto& succ : it->second) {
							if (visited.insert(succ.rid()).second) {
								q.push(succ);
							}
						}
					}
				}
			}
		}

		out << "  }\n";
	};

	emit_stage("early_tick", tick_schedule.early_tick);
	emit_stage("tick", tick_schedule.tick);
	emit_stage("post_physics", tick_schedule.post_physics);
	emit_stage("late_tick", tick_schedule.late_tick);

	out << "}\n";
	return out.str();
}

#pragma region TESTS

namespace _detail {

auto WorldTestAccess::createWorld() -> World* {
	return new World();
}

auto WorldTestAccess::createNode(World& world, std::string_view name, NodeState state) -> Box<Node> {
	auto node = world.nodeAllocation();
	node->table = new NodeFunctionTable();
	node->m.name = name;
	node->m.state = state;
	node->m.type = NodeType::child;
	node->m.local_enabled = true;
	node->m.inherited_enabled = true;
	return node;
}

void WorldTestAccess::registerDependency(Node& from, Node& to) {
	World::registerDependency(from, to);
}

auto WorldTestAccess::functionTable(Node& node) noexcept -> NodeFunctionTable& {
	return *node.table;
}

auto WorldTestAccess::functionTable(const Node& node) noexcept -> const NodeFunctionTable& {
	return *node.table;
}

auto WorldTestAccess::tickSchedule(World& world) noexcept -> _detail::TickSchedule& {
	return world.tick_schedule;
}

auto WorldTestAccess::dependencyGraph(World& world) noexcept -> World::DependencyGraph& {
	return world.dependency_graph;
}

void WorldTestAccess::computeDependencyGraph(World& world) {
	world.computeDependencyGraph();
}

auto WorldTestAccess::dependencyGraphGraphviz(const World& world) -> std::string {
	return world.dependencyGraphGraphviz();
}

}    // namespace _detail

#pragma endregion TESTS

}
