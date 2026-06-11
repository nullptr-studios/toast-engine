#include "world.hpp"

#include "node_3d.hpp"
#include "world_test_access.hpp"

#include <chrono>
#include <sstream>
#include <toast/assets/assets.hpp>
#include <toast/assets/types.hpp>
#include <toast/thread_pool.hpp>
#include <utility>

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
		if (node->info() && node->info()->hasFunction(TickFunctionList::early_tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasTick() -> bool {
	for (auto& node : nodes) {
		if (node->info() && node->info()->hasFunction(TickFunctionList::tick)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasPostPhysics() -> bool {
	for (auto& node : nodes) {
		if (node->info() && node->info()->hasFunction(TickFunctionList::post_physics)) {
			return true;
		}
	}
	return false;
}

auto NodeCluster::hasLateTick() -> bool {
	for (auto& node : nodes) {
		if (node->info() && node->info()->hasFunction(TickFunctionList::late_tick)) {
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

auto World::nodeAllocation(std::optional<assets::Prefab::BasicNode> node_data) noexcept -> Box<Node> {
	ZoneScoped;

	// Identify the type and get reflection info
	std::string type = node_data.has_value() ? node_data->type : "toast::Node";
	const NodeInfo* info = m.node_registry.reflect(type);

#ifndef NDEBUG
	if (!info) {
		TOAST_WARN("World", "Reflection information for type {} not found. Falling back to toast::Node", type);
		info = m.node_registry.reflect("toast::Node");
	}
#endif

	// Node allocation
#ifdef NDEBUG
	Node* raw_node = info->construct();
#else
	Node* raw_node = (info && info->construct) ? info->construct() : new Node();
#endif

	{
		std::scoped_lock lock(m.nodes_mutex);
		auto [it, result] = m.nodes.emplace(raw_node);
		TOAST_ASSERT(result, "World", "Node allocation failed");
	}
	raw_node->m_info = info;    // attach reflection data

	if (node_data.has_value()) {
		raw_node->name(node_data->name);

		if (info) {
			info->forEachBaseType([&](const NodeInfo& level) {
				for (const auto& f : level.all_fields) {
					auto f_data = node_data->find(f.name);
					if (not f_data.has_value()) {
						continue;
					}

#ifndef NDEBUG
					if (not f.set) {
						TOAST_WARN("World", "No valid set function found for {}", f.name);
						continue;
					}
#endif

					f.set(raw_node, f_data->value);
				}
			});
		}
	}

	return raw_node->box();
}

void World::tick() {
	drainDestroyQueue();
	drainLoadQueue();

	auto run_phase = [](const std::vector<TickSchedule::Wave>& phase, TickFunctionList func) {
		for (const auto& wave : phase) {
			std::vector<std::future<void>> futures;
			futures.reserve(wave.size());

			for (const auto& n : wave) {
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

			for (auto& f : futures) {
				f.get();
			}
		}
	};

	run_phase(tick_schedule.early_tick, TickFunctionList::early_tick);
	run_phase(tick_schedule.tick, TickFunctionList::tick);
	// TODO: physics step goes between tick and post_physics
	run_phase(tick_schedule.post_physics, TickFunctionList::post_physics);
	run_phase(tick_schedule.late_tick, TickFunctionList::late_tick);
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

	// don't store duplicates
	auto& edges = instance->dependency_graph.connections[from];
	if (std::ranges::contains(edges, Box<Node>(to))) {
		return;
	}

	edges.emplace_back(to);
	instance->dependency_graph.inverse_connections[to].emplace_back(from);
}

auto World::requestRuntimeCreation(Node& parent) -> Box<Node> {
	ZoneScoped;

	// Phase 1: allocation
	Box node = instance->nodeAllocation();
	// no load since they are not being serialized
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Phase 2: data structure generation
	node->m_uid.generate();
	node->m_parent = parent;
	parent.m_children.emplace_back(node);

	registerDependency(parent, node);
	std::ranges::transform(parent.m_wave, std::begin(node->m_wave), [](auto x) { return (x < 255) ? x + 1 : x; });
	auto schedule_node = [node](auto& schedule_vec, uint8_t wave_idx) {
		if (wave_idx >= schedule_vec.size()) {
			schedule_vec.resize(wave_idx + 1);
		}
		schedule_vec[wave_idx].emplace_back(node);
	};
	schedule_node(instance->tick_schedule.early_tick, node->m_wave[0]);
	schedule_node(instance->tick_schedule.tick, node->m_wave[1]);
	schedule_node(instance->tick_schedule.post_physics, node->m_wave[2]);
	schedule_node(instance->tick_schedule.late_tick, node->m_wave[3]);

	node->m_state = parent.m_state;
	node->m_type = NodeType::child;
	node->m_inherited_enabled = parent.enabled();

	// Phase 3: node initialization
	node->callTick(node->info(), TickFunctionList::pre_init);
	node->enabled(true);
	return node;
}

void World::loadNode(UID uid) {
	// Load stages:
	//		1: get the node_file
	//		2: dispatch 1:
	//				- control block allocation
	//				- deserialize
	//				- preInit()
	//		3: data structure building:
	//				- build tree structure
	//		4: dispatch 2:
	//				- init()

	auto future = std::async(std::launch::async, [uid]() {
		auto node_file = assets::load<assets::Prefab>(uid);
		if (not node_file.hasValue()) {
			TOAST_ERROR("World", "Couldn't load Node {}", uid);
			return;
		}

		// parallel allocation + deserialization
		std::vector<std::future<Box<Node>>> alloc_futures;
		std::vector<Box<Node>> nodes;
		size_t size = node_file->nodes.size();
		alloc_futures.reserve(size);
		nodes.reserve(size);
		for (const auto& node : node_file->nodes) {
			alloc_futures.emplace_back(ThreadPool::push([node_data = node]() {
				Box node = instance->nodeAllocation(node_data);
				node->callTick(node->info(), TickFunctionList::load);
				node->callTick(node->info(), TickFunctionList::pre_init);    // TODO: deprecate pre_init
				node->m_state = NodeState::loading;
				return node;
			}));
		}

		for (auto& f : alloc_futures) {
			nodes.emplace_back(f.get());
		}

		Box<Node> root = instance->buildTree(std::move(nodes), node_file);

		root->propagateCallTick(root->info(), TickFunctionList::init);

		std::scoped_lock lock(instance->m.load_mutex);
		instance->nodes.load_queue.emplace_back(std::move(root));
	});

	std::scoped_lock lock(instance->m.load_mutex);
	std::erase_if(instance->m.load_futures, [](std::future<void>& f) {
		return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	});
	instance->m.load_futures.emplace_back(std::move(future));
}

void World::loadNode(std::string_view uri) {
	// just reroute to the actual loadNode() implementation
	auto id = assets::resolveURI(uri);

#ifndef NDEBUG
	if (not id.has_value()) {
		TOAST_WARN("World", "Couldn't load Node {}", uri);
		return;
	}
#endif

	loadNode(*id);
}

void World::drainLoadQueue() {
	std::vector<Box<Node>> loaded;
	{
		std::scoped_lock lock(m.load_mutex);
		if (nodes.load_queue.empty()) {
			return;
		}
		std::swap(loaded, nodes.load_queue);
	}

	// Freshly loaded trees go to the cached list and are ready to be activated
	for (auto& root : loaded) {
		root->changeNodeState(NodeState::cached);
		nodes.cached.emplace_back(std::move(root));
	}
}

auto World::setRoot(Node& node) -> Box<Node> {
	return instance->swapRoot(node);
}

auto World::cacheNode(Node& node) -> Box<Node> {
	return instance->moveToCached(node);
}

auto World::findCached(std::string_view name) -> Box<Node> {
	for (const auto& node : instance->nodes.cached) {
		if (node->name() == name) {
			return node;
		}
	}
	return {};
}

auto World::graphviz() -> std::string {
	return instance->dependencyGraphGraphviz();
}

void World::destroyNode(Node& node) {
	if (node.m_state != NodeState::cached) {
		TOAST_WARN("World", "Only cached nodes can be destroyed, {} ({}) is still in use", node.name(), node.uid());
		return;
	}

	std::erase(instance->nodes.cached, node.box());
	node.changeNodeState(NodeState::destroy);
	instance->nodes.destroy_queue.emplace_back(node.box());
}

void World::drainDestroyQueue() {
	std::vector<Box<Node>> doomed;
	std::swap(doomed, nodes.destroy_queue);

	if (!doomed.empty()) {
		ZoneScopedN("World::drainDestroyQueue()");

		// Run the destroy lifecycle while the trees are still intact
		for (auto& root : doomed) {
			root->propagateCallTick(root->info(), TickFunctionList::destroy);
		}

		// Collect every node of every doomed tree
		std::vector<Node*> victims;
		auto collect = [&victims](this auto&& self, Node& node) -> void {
			victims.push_back(&node);
			for (auto& child : node.m_children) {
				self(*child);
			}
		};
		for (auto& root : doomed) {
			collect(*root);
		}
		doomed.clear();    // drop our own references before tearing the trees down

		std::unordered_set<const Node*> victim_set(victims.begin(), victims.end());

		// Scrub every edge that involves a victim from the dependency graph
		auto scrub = [&](std::unordered_map<Box<Node>, std::vector<Box<Node>>>& connections) {
			for (Node* victim : victims) {
				connections.erase(victim->box());
			}
			for (auto& [node, edges] : connections) {
				std::erase_if(edges, [&](const Box<Node>& edge) { return edge.exists() && victim_set.contains(&*edge); });
			}
		};
		scrub(dependency_graph.connections);
		scrub(dependency_graph.inverse_connections);

		// Detach the tree structure so no victim holds a Box to another
		for (Node* victim : victims) {
			victim->m_parent = {};
			victim->m_children.clear();
			victim->m_listener.reset();
		}

		// Free the nodes. The control box is tombstoned (node = nullptr) instead of freed,
		// so any stale Box<Node> still held elsewhere safely reports exists() == false
		{
			std::scoped_lock lock(m.nodes_mutex);
			for (Node* victim : victims) {
				ControlBox* control = ControlBox::get(victim);
				const NodeInfo* info = victim->info();
				TOAST_TRACE("World", "Destroying node {} ({})", victim->name(), victim->uid());

				if (info && info->destroy) {
					info->destroy(victim);
				} else {
					delete victim;
				}
				control->node = nullptr;
				m.tombstones++;
			}
		}

		computeDependencyGraph();
	}

	// Reap tombstones once their last Box released them
	if (m.tombstones > 0) {
		std::scoped_lock lock(m.nodes_mutex);
		m.tombstones -= std::erase_if(m.nodes, [](const ControlBox& control) {
			return control.node == nullptr && control.ref_count.load(std::memory_order_acquire) == 0;
		});
	}
}

void World::markNode3DDependantsDirty(const Box<Node>& node) noexcept {
	if (!instance) {
		return;
	}

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
	std::vector<std::vector<Box<Node>>> subgraphs;
	{
		// Loader threads insert into m.nodes concurrently
		std::scoped_lock lock(m.nodes_mutex);

		// Guarantee every existing node exists in the subgraph
		for (const auto& node : m.nodes) {
			if (!node.node || node.node->m_state == NodeState::destroy) {
				continue;
			}
			dependency_graph.connections[node.node->box()];
			dependency_graph.inverse_connections[node.node->box()];
		}

		subgraphs = subgraphSeparation();
	}

	// We are gonna remove nodes that will not be able to get ticked because:
	//		1) they don't have any tick functions
	//		2) they are not in an active state (cached, mid-load, etc. don't tick)
	for (auto& g : subgraphs) {
		std::erase_if(g, [](const auto& n) { return not n->info() || not n->info()->hasFunction(TickFunctionList::tick_mask); });

		std::erase_if(g, [](const auto& n) { return n->m_state != NodeState::root && n->m_state != NodeState::global; });
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
		if (!start_cn.node || start_cn.node->m_state == NodeState::destroy) {
			continue;
		}
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

auto World::tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& input_subgraphs) -> std::vector<TickSchedule::Wave> {
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

	std::vector<TickSchedule::Wave> result;

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

auto World::assignWaves(const std::vector<TickSchedule::Wave>& subgraphs) -> std::vector<TickSchedule::Wave> {
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
	std::vector<TickSchedule::Wave> buckets(global_max_wave + 1);

	for (int subgraph_idx = 0; std::cmp_less(subgraph_idx, subgraphs.size()); ++subgraph_idx) {
		for (int item_idx = 0; std::cmp_less(item_idx, subgraphs[subgraph_idx].size()); ++item_idx) {
			buckets[item_wave_levels[subgraph_idx][item_idx]].emplace_back(subgraphs[subgraph_idx][item_idx]);
		}
	}

	return buckets;
}

auto World::optimizeWaves(const std::vector<TickSchedule::Wave>& waves) -> TickSchedule {
	TickSchedule schedule = {
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

	filter_and_assign_wave(schedule.early_tick, 0, [](auto n) {
		return n->info() && n->info()->hasFunction(TickFunctionList::early_tick);
	});
	filter_and_assign_wave(schedule.tick, 1, [](auto n) { return n->info() && n->info()->hasFunction(TickFunctionList::tick); });
	filter_and_assign_wave(schedule.post_physics, 2, [](auto n) {
		return n->info() && n->info()->hasFunction(TickFunctionList::post_physics);
	});
	filter_and_assign_wave(schedule.late_tick, 3, [](auto n) {
		return n->info() && n->info()->hasFunction(TickFunctionList::late_tick);
	});

	return schedule;
}

auto World::swapRoot(Node& node) -> Box<Node> {
	ZoneScoped;
	ZoneNameF("World::swapRoot() [to %s]", node.name().data());

	if (node.m_type != NodeType::root) {
		TOAST_ERROR("World", "You can only swap roots with a root node");
		return {};
	}

	// The world may be empty; in that case the node is simply promoted
	auto root_node = nodes.root;

	switch (node.m_state) {
		case NodeState::root: TOAST_WARN("World", "Attempted to swap root with a node already in root"); return {};
		case NodeState::destroy:
		case NodeState::loading:
		case NodeState::null:
			TOAST_WARN("World", "Attempted to swap root with a not initialized node or one that is scheduled for destruction");
			return {};
		case NodeState::cached:
			if (root_node.exists()) {
				std::ranges::replace(nodes.cached, node.box(), root_node);
				root_node->m_type = NodeType::root;    // world_root only exists in root and global
				root_node->changeNodeState(NodeState::cached);
				root_node->propagateCallTick(root_node->info(), TickFunctionList::end);
				root_node->enabled(false);
			} else {
				std::erase(nodes.cached, node.box());
			}
			break;
		case NodeState::global:
			if (root_node.exists()) {
				std::ranges::replace(nodes.global, node.box(), root_node);
				root_node->changeNodeState(NodeState::global);
			} else {
				std::erase(nodes.global, node.box());
			}
			break;
	}

	node.m_type = NodeType::world_root;
	node.changeNodeState(NodeState::root);
	nodes.root = node.box();

	computeDependencyGraph();

	node.propagateCallTick(node.info(), TickFunctionList::begin);
	node.enabled(true);

	return root_node;
}

auto World::moveToCached(Node& node) -> Box<Node> {
	ZoneScoped;
	ZoneNameF("World::moveToCached() [%s]", node.name().data());

	switch (node.m_state) {
		case NodeState::cached: TOAST_WARN("World", "Tried to move to cache a node already on cache"); return {};
		case NodeState::destroy:
		case NodeState::null:
		case NodeState::loading:
			TOAST_WARN("World", "Tried to move to cache a node that is not initialized or scheduled for destruction");
			return {};
		case NodeState::root:
			if (node.m_type == NodeType::world_root) {
				// Caching the active root leaves the world empty
				nodes.root = {};
			} else {
				std::erase(node.parent()->m_children, node.box());
				node.m_parent = {};
			}
			break;
		case NodeState::global:
			if (node.m_type == NodeType::world_root) {
				std::erase(nodes.global, node.box());
			} else {
				std::erase(node.parent()->m_children, node.box());
				node.m_parent = {};
			}
			break;
	}

	node.changeNodeState(NodeState::cached);
	node.enabled(false);
	node.propagateCallTick(node.info(), TickFunctionList::end);

	// ensure there's only root nodes or children nodes, no world_root
	// world_root only exists in global and root
	if (node.m_type == NodeType::world_root) {
		node.m_type = NodeType::root;
	}

	nodes.cached.emplace_back(node.box());

	computeDependencyGraph();
	return node.box();
}

auto World::moveToGlobal(Node& node) -> Box<Node> {
	switch (node.m_state) {
		case NodeState::root: TOAST_ERROR("World", "Tried to move root to cached, consider using swapRoot() instead"); return {};
		case NodeState::global: TOAST_WARN("World", "Tried to move to global a Node that is already in global"); return {};
		case NodeState::destroy:
		case NodeState::null:
		case NodeState::loading:
			TOAST_WARN("World", "Tried to move to cache a node that is not initialized or scheduled for destruction");
			return {};
		case NodeState::cached: break;
	}

	if (node.m_type == NodeType::child) {
		TOAST_ERROR("World", "Only Root nodes can be moved to global, {} is a children node", nodes.root->name().data());
	}

	std::erase(nodes.cached, node.box());

	node.m_type = NodeType::world_root;
	node.changeNodeState(NodeState::global);

	computeDependencyGraph();

	node.propagateCallTick(node.info(), TickFunctionList::begin);
	node.enabled(true);
	nodes.global.emplace_back(node.box());
	return node.box();
}

auto World::moveToChild(Node& node, Node& parent) -> Box<Node> {
	if (parent.m_state != NodeState::root) {
		TOAST_ERROR("World", "You can only move a node into one that is on the root");
		return {};
	}

	switch (node.m_state) {
		case NodeState::null:
		case NodeState::loading:
		case NodeState::destroy:
			TOAST_WARN("World", "Tried to move to cache a node that is not initialized or scheduled for destruction");
			return {};
		case NodeState::root:
			if (node.m_type == NodeType::world_root) {
				TOAST_ERROR("World", "Tried to move root to cached, consider using swapRoot() instead");
				return {};
			}
			break;
		case NodeState::global:
		case NodeState::cached: break;
	}

	bool run_begin = false;

	if (node.parent().exists()) {
		std::erase(node.parent()->m_children, node.box());
	} else {
		if (node.m_state == NodeState::global) {
			std::erase(nodes.global, node.box());
		} else if (node.m_state == NodeState::cached) {
			std::erase(nodes.cached, node.box());
			run_begin = true;
		}
	}

	node.changeNodeState(parent.m_state);
	if (run_begin) {
		node.propagateCallTick(node.info(), TickFunctionList::begin);
		node.enabled(true);
	}
	parent.m_children.emplace_back(node.box());
	node.m_parent = parent;

	return node.box();
}

auto World::buildTree(std::vector<Box<Node>>&& nodes, const assets::AssetHandle<assets::Prefab>& file) -> Box<Node> {
	std::unordered_map<uint64_t, Box<Node>> uid_map;
	uid_map.reserve(nodes.size());
	for (auto& node : nodes) {
		uid_map[node->uid().data()] = node;
	}

	Box<Node> root;

	// Prefab serialization guarantees exactly one rootless node, written first.
	// nodes[i] pairs with file->nodes[i]: the alloc futures are collected in
	// submission order, which is the file order.
	for (size_t i = 0; i < nodes.size(); ++i) {
		auto& node = nodes[i];
		const auto& data = file->nodes[i];

		auto parent_field = data.find("Parent");
		bool has_parent = false;

		if (parent_field.has_value()) {
			try {
				UID parent_uid = parent_field->as<UID>();

				auto it = uid_map.find(parent_uid.data());
				if (it != uid_map.end()) {
					node->m_parent = it->second;
					it->second->m_children.emplace_back(node);
					has_parent = true;
				}
			} catch (const std::bad_any_cast&) {
				TOAST_WARN("World", "Cast to UID of field Parent in {} failed, treating as Root", data.name);
			}
		}

		node->m_type = has_parent ? NodeType::child : NodeType::root;

		if (not has_parent) {
#ifndef NDEBUG
			if (root.exists()) {
				TOAST_WARN("World", "Multiple rootless nodes in Prefab ({} and {}), keeping the last one", root->name(), data.name);
			}
#endif
			root = node;
		}
	}

#ifndef NDEBUG
	if (not root.exists()) {
		TOAST_ERROR("World", "Prefab contains no rootless node, the loaded tree has no root");
	}
#endif

	// Serialization only stores the local enabled flag; the inherited one is derived from the tree
	if (root.exists()) {
		auto propagate_inherited = [](this auto&& self, Node& n, bool inherited) -> void {
			n.m_inherited_enabled = inherited;
			for (auto& child : n.m_children) {
				self(*child, n.enabled());
			}
		};
		propagate_inherited(*root, true);
	}

	return root;
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
	node->m_name = name;
	node->m_state = state;
	node->m_type = NodeType::child;
	node->m_local_enabled = true;
	node->m_inherited_enabled = true;
	return node;
}

void WorldTestAccess::registerDependency(Node& from, Node& to) {
	World::registerDependency(from, to);
}

void WorldTestAccess::addTickStage(Node& node, TickFunctionList stage) {
	// Test-only: fabricate a per-instance NodeInfo carrying the requested tick flags so the
	// scheduler (which reads m_info) treats this node as participating in `stage`. Real nodes
	// get their NodeInfo from the type registry instead.
	static std::unordered_map<const Node*, NodeInfo> infos;
	NodeInfo& info = infos[&node];
	info.type = "test::Node";
	info.functions.list = info.functions.list | stage;
	node.m_info = &info;
}

auto WorldTestAccess::tickSchedule(World& world) noexcept -> TickSchedule& {
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
