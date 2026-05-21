#include "world.hpp"

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
		TOAST_WARN("World", "{} ({}) tried to register a dependency to itself", from.name(), from.uuid());
		return;
	}
	// TODO: sanity check
	// auto& node_set = instance->m.nodes;
	// if (node_set.find(from.box()) == node_set.end()) {
	//
	// }

	instance->graph.connections[from].emplace_back(to);
	instance->graph.inverse_connections[to].emplace_back(from);
}

auto World::requestRuntimeCreation(Node& parent) -> Box<Node> {
	ZoneScoped;

	// Phase 1: allocation
	Box node = instance->nodeAllocation();
	node->table->load(node);
	node->table->preInit(node);

	// Phase 2: data structure generation
	node->m.uuid.generate();
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
		r.get()->m.uuid.generate();
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

void World::computeDependencyGraph() {
	// Guarantee every existing node exists in the subgraph
	for (const auto& node : m.nodes) {
		graph.connections[node.node->box()];
		graph.inverse_connections[node.node->box()];
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

auto World::tarjanAlgorithm(const std::vector<std::vector<Box<Node>>>& sg) -> std::vector<_detail::TickSchedule::Wave> {
	struct TarjanContext {
		std::unordered_map<Box<Node>, int> index;
		std::unordered_map<Box<Node>, int> low_link;
		std::unordered_map<Box<Node>, bool> on_stack;
		std::stack<Box<Node>> stack;
		int counter = 0;
		std::vector<std::vector<Box<Node>>> sccs;    // reverse topology order
	};

	std::vector<_detail::TickSchedule::Wave> subgraphs;
	subgraphs.reserve(sg.size());
	for (const auto& v : sg) {
		_detail::TickSchedule::Wave wave;
		wave.reserve(v.size());
		for (const auto& w : v) {
			wave.emplace_back(w);
		}
		subgraphs.emplace_back(std::move(wave));
	}

	std::vector<_detail::TickSchedule::Wave> result;

	for (const auto& g : subgraphs) {
		// Build surviving nodes for edge filtering
		std::unordered_set<Box<Node>> in_subgraph;
		for (const auto& v : g) {
			in_subgraph.insert(std::get<Box<Node>>(v));
		}

		TarjanContext ctx;
		std::function<void(const Box<Node>&)> strong_connect = [&](const Box<Node>& v) {
			ctx.index[v] = ctx.low_link[v] = ctx.counter++;
			ctx.stack.push(v);
			ctx.on_stack[v] = true;

			if (auto it = graph.connections.find(v); it != graph.connections.end()) {
				for (const auto& w : it->second) {
					if (!in_subgraph.contains(w)) {
						// Skip nodes pruned in phase 2
						continue;
					}

					if (!ctx.index.contains(w)) {
						strong_connect(w);
						ctx.low_link[v] = std::min(ctx.low_link[v], ctx.low_link[w]);
					} else if (ctx.on_stack[w]) {
						ctx.low_link[v] = std::min(ctx.low_link[v], ctx.low_link[w]);
					}
				}
			}

			// Root of an SCC: we pop it
			if (ctx.low_link[v] == ctx.index[v]) {
				std::vector<Box<Node>> scc;
				while (true) {
					auto w = ctx.stack.top();
					ctx.stack.pop();
					ctx.on_stack[w] = false;
					scc.push_back(w);
					if (w == v) {
						break;
					}
				}
				ctx.sccs.emplace_back(std::move(scc));
			}
		};

		for (const auto& variant : g) {
			const auto& node = std::get<Box<Node>>(variant);
			if (!ctx.index.contains(node)) {
				strong_connect(node);
			}
		}

		// SCCs is in reverse order so
		_detail::TickSchedule::Wave sorted_g;
		sorted_g.reserve(ctx.sccs.size());

		for (auto& scc : ctx.sccs) {
			if (scc.size() == 1) {
				sorted_g.emplace_back(scc[0]);
			} else {
				TOAST_TRACE("World", "Dependency cycle detected ({} nodes) -> grouping into NodeCluster", scc.size());
				sorted_g.emplace_back(_detail::NodeCluster(scc));
			}
		}
		result.emplace_back(std::move(sorted_g));
	}

	return result;
}

auto World::assignWaves(const std::vector<_detail::TickSchedule::Wave>& subgraphs) -> std::vector<_detail::TickSchedule::Wave> {
	// Map every node back to its containing item so NodeClusters are treated as a single scheduling unit
	struct ItemRef {
		int subgraph;
		int index;
	};

	std::unordered_map<Box<Node>, ItemRef> node_to_item;

	for (int si = 0; std::cmp_less(si, subgraphs.size()); ++si) {
		for (int ii = 0; std::cmp_less(ii, subgraphs[si].size()); ++ii) {
			std::visit(
			    [&](const auto& item) {
				    using T = std::decay_t<decltype(item)>;
				    if constexpr (std::is_same_v<T, Box<Node>>) {
					    node_to_item[item] = {si, ii};
				    } else {    // NodeCluster
					    for (const auto& n : item.nodes) {
						    node_to_item[n] = {si, ii};
					    }
				    }
			    },
			    subgraphs[si][ii]
			);
		}
	}

	// Assign wave levels
	std::vector<std::vector<int>> waves(subgraphs.size());
	for (int si = 0; std::cmp_less(si, subgraphs.size()); ++si) {
		waves[si].assign(subgraphs[si].size(), 0);
	}

	for (int si = 0; std::cmp_less(si, subgraphs.size()); ++si) {
		// Reverse iteration because of tarjan
		for (int ii = (int)subgraphs[si].size() - 1; ii >= 0; --ii) {
			// Collect all physical nodes belonging to this item
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
			    subgraphs[si][ii]
			);

			int max_pred_wave = -1;
			for (const auto& node : item_nodes) {
				auto it = graph.inverse_connections.find(node);
				if (it == graph.inverse_connections.end()) {
					continue;
				}

				for (const auto& pred : it->second) {
					auto pit = node_to_item.find(pred);
					if (pit == node_to_item.end()) {
						continue;    // pruned in Phase 2
					}

					auto [psi, pii] = pit->second;
					if (psi == si && pii != ii) {    // skip intra-cluster self-edges
						max_pred_wave = std::max(max_pred_wave, waves[psi][pii]);
					}
				}
			}
			waves[si][ii] = max_pred_wave + 1;
		}
	}

	// Find the highest wave across all subgraphs
	int max_wave = 0;
	for (const auto& sg : waves) {
		for (int w : sg) {
			max_wave = std::max(max_wave, w);
		}
	}

	// Bucket items by wave level
	// Each bucket is a parallel dispatch barrier: all items within a bucket can run concurrently
	// The next bucket will then only start when all futures join
	std::vector<_detail::TickSchedule::Wave> bucket(max_wave + 1);

	for (int si = 0; std::cmp_less(si, subgraphs.size()); ++si) {
		for (int ii = 0; std::cmp_less(ii, subgraphs[si].size()); ++ii) {
			bucket[waves[si][ii]].emplace_back(subgraphs[si][ii]);
		}
	}

	return bucket;
}

auto World::optimizeWaves(const std::vector<_detail::TickSchedule::Wave>& waves) -> _detail::TickSchedule {
	_detail::TickSchedule ts = {
	  .early_tick = waves,
	  .tick = waves,
	  .post_physics = waves,
	  .late_tick = waves,
	};

	for (auto& wave : ts.early_tick) {
		std::erase_if(wave, [](std::variant<Box<Node>, _detail::NodeCluster>& item) -> bool {
			if (std::holds_alternative<Box<Node>>(item)) {
				auto node = std::get<Box<Node>>(item);
				return node->table->table.early_tick.empty();
			}

			const auto& cluster = std::get<_detail::NodeCluster>(item);
			return !std::ranges::any_of(cluster.nodes, [](const auto& node) { return !node->table->table.early_tick.empty(); });
		});
	}
	std::erase_if(ts.early_tick, [](const auto& wave) { return wave.empty(); });
	for (int i = 0; i < ts.early_tick.size(); ++i) {
		for (const auto& variant : ts.early_tick[i]) {
			if (std::holds_alternative<Box<Node>>(variant)) {
				auto node = std::get<Box<Node>>(variant);
				node->m.wave[0] = i;
			} else {
				const auto& cluster = std::get<_detail::NodeCluster>(variant);
				for (auto node : cluster.nodes) {
					node->m.wave[0] = i;
				}
			}
		}
	}

	for (auto& wave : ts.tick) {
		std::erase_if(wave, [](std::variant<Box<Node>, _detail::NodeCluster>& item) -> bool {
			if (std::holds_alternative<Box<Node>>(item)) {
				auto node = std::get<Box<Node>>(item);
				return node->table->table.tick.empty();
			}

			const auto& cluster = std::get<_detail::NodeCluster>(item);
			return !std::ranges::any_of(cluster.nodes, [](const auto& node) { return !node->table->table.tick.empty(); });
		});
	}
	std::erase_if(ts.tick, [](const auto& wave) { return wave.empty(); });
	for (int i = 0; i < ts.tick.size(); ++i) {
		for (const auto& variant : ts.tick[i]) {
			if (std::holds_alternative<Box<Node>>(variant)) {
				auto node = std::get<Box<Node>>(variant);
				node->m.wave[1] = i;
			} else {
				const auto& cluster = std::get<_detail::NodeCluster>(variant);
				for (auto node : cluster.nodes) {
					node->m.wave[1] = i;
				}
			}
		}
	}

	for (auto& wave : ts.post_physics) {
		std::erase_if(wave, [](std::variant<Box<Node>, _detail::NodeCluster>& item) -> bool {
			if (std::holds_alternative<Box<Node>>(item)) {
				auto node = std::get<Box<Node>>(item);
				return node->table->table.post_physics.empty();
			}

			const auto& cluster = std::get<_detail::NodeCluster>(item);
			return !std::ranges::any_of(cluster.nodes, [](const auto& node) { return !node->table->table.post_physics.empty(); });
		});
	}
	std::erase_if(ts.post_physics, [](const auto& wave) { return wave.empty(); });
	for (int i = 0; i < ts.post_physics.size(); ++i) {
		for (const auto& variant : ts.post_physics[i]) {
			if (std::holds_alternative<Box<Node>>(variant)) {
				auto node = std::get<Box<Node>>(variant);
				node->m.wave[2] = i;
			} else {
				const auto& cluster = std::get<_detail::NodeCluster>(variant);
				for (auto node : cluster.nodes) {
					node->m.wave[2] = i;
				}
			}
		}
	}

	for (auto& wave : ts.late_tick) {
		std::erase_if(wave, [](std::variant<Box<Node>, _detail::NodeCluster>& item) -> bool {
			if (std::holds_alternative<Box<Node>>(item)) {
				auto node = std::get<Box<Node>>(item);
				return node->table->table.late_tick.empty();
			}

			const auto& cluster = std::get<_detail::NodeCluster>(item);
			return !std::ranges::any_of(cluster.nodes, [](const auto& node) { return !node->table->table.late_tick.empty(); });
		});
	}
	std::erase_if(ts.late_tick, [](const auto& wave) { return wave.empty(); });
	for (int i = 0; i < ts.late_tick.size(); ++i) {
		for (const auto& variant : ts.late_tick[i]) {
			if (std::holds_alternative<Box<Node>>(variant)) {
				auto node = std::get<Box<Node>>(variant);
				node->m.wave[3] = i;
			} else {
				const auto& cluster = std::get<_detail::NodeCluster>(variant);
				for (auto node : cluster.nodes) {
					node->m.wave[3] = i;
				}
			}
		}
	}

	return ts;
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

			if (auto it = graph.connections.find(node); it != graph.connections.end()) {
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
					if (auto it = graph.connections.find(curr); it != graph.connections.end()) {
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

}
