/**
 * @file World.hpp
 * @author Xein
 * @date 18 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "../log.hpp"
#include "box.hpp"
#include "control_box.hpp"
#include "node.hpp"
#include "world_events.hpp"

#include <future>
#include <toast/events/listener.hpp>
#include <unordered_set>
#include <variant>

namespace toast {

class Node;

struct NodeCluster {
	NodeCluster(std::vector<Box<Node>>& nodes) : nodes(nodes) {}
	std::vector<Box<Node>> nodes;
	void earlyTick() {
		for (auto& node : nodes) {
			node->table->earlyTick(*node);
		}
	}
	void tick() {
		for (auto& node : nodes) {
			node->table->tick(*node);
		}
	}
	void postPhysics() {
		for (auto& node : nodes) {
			node->table->postPhysics(*node);
		}
	}
	void lateTick() {
		for (auto& node : nodes) {
			node->table->lateTick(*node);
		}
	}
};

class World {
public:
	World() {
		instance = this;
		m.listener.subscribe<event::SwapWorldRoot>([this](const auto& e) { swapRoot(e.node); });
	}

	~World() = default;

	/**
	 * Stores a new dependency
	 * @param from Node that will be ticked first
	 * @param to Node that will be ticked last
	 */
	static auto registerDependency(Node& from, Node& to) {
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

	/**
	 * @brief Requests to create a new node during runtime
	 */
	static auto requestRuntimeCreation(Node& parent) -> Box<Node>;
	static void dispatchNodeCreation(int count);

	void computeDependencyGraph() {
		// Guarantee every existing node exists in the subgraph
		for (auto& node : m.nodes) {
			graph.connections[node.node->box()];
			graph.inverse_connections[node.node->box()];
		}

		// Phase 1: BFS flood algorithm
		// This separates the graph into multiple subgraphs that are independent of each other
		std::unordered_set<Box<Node>> visited;
		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> subgraphs;

		for (const auto& start_cn : m.nodes) {
			auto start_node = start_cn.node->box();
			if (visited.count(start_node)) {
				continue;
			}

			std::vector<Box<Node>> component;
			std::queue<Box<Node>> queue;

			queue.push(start_node);
			visited.insert(start_node);

			while (!queue.empty()) {
				auto current = queue.front();
				queue.pop();
				component.push_back(current);

				// Walk forward edges
				if (auto it = graph.connections.find(current->box()); it != graph.connections.end()) {
					for (const auto& neighbor : it->second) {
						if (visited.count(neighbor)) {
							continue;
						}
						visited.insert(neighbor);
						queue.push(neighbor);
					}
				}

				// Walk backward edges
				if (auto it = graph.inverse_connections.find(current->box()); it != graph.inverse_connections.end()) {
					for (const auto& neighbor : it->second) {
						if (visited.count(neighbor)) {
							continue;
						}
						visited.insert(neighbor);
						queue.push(neighbor);
					}
				}
			}

			std::vector<std::variant<Box<Node>, NodeCluster>> subgraph;
			subgraph.reserve(component.size());
			subgraph.assign(std::make_move_iterator(component.begin()), std::make_move_iterator(component.end()));
			subgraphs.push_back(std::move(subgraph));
		}

		// Phase 2: Now we can discard some nodes to make the graph quicker to compute
		// We are gonna remove nodes that 1) don't have any tick functions and 2) are in the NodeState::cached
		// as those nodes will not be able to be ticked
		// OPTIMIZE: We will be able to generate multiple tick functions per function type later on
		for (auto& g : subgraphs) {
			std::erase_if(g, [](const auto& n_variant) {
				const Box<Node>& n = std::get<0>(n_variant);
				return n->table->table.early_tick.empty() && n->table->table.tick.empty() && n->table->table.post_physics.empty() &&
				       n->table->table.late_tick.empty();
			});

			std::erase_if(g, [](const auto& n_variant) {
				const Box<Node>& n = std::get<0>(n_variant);
				return n->m.state == NodeState::cached;
			});
		}

		// Drop subgraphs that became entirely empty
		std::erase_if(subgraphs, [](const auto& g) { return g.empty(); });

		// Phase 3: Tarjan algorithm and cluster nodes creation
		// We are going to detect SCCs using the Tarjan algorithm and then create ClusterNodes from those SCCs so they
		// are ticked synchronously and don't result in any race condition
		struct TarjanContext {
			std::unordered_map<Box<Node>, int> index;
			std::unordered_map<Box<Node>, int> low_link;
			std::unordered_map<Box<Node>, bool> on_stack;
			std::stack<Box<Node>> stack;
			int counter = 0;
			std::vector<std::vector<Box<Node>>> SCCs; // reverse topology order
		};

		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> result_subgraphs;

		for (auto& g : subgraphs) {
			// Build surviving nodes for edge filtering
			std::unordered_set<Box<Node>> in_subgraph;
			for (auto& v : g) in_subgraph.insert(std::get<Box<Node>>(v));

			TarjanContext ctx;
			std::function<void(const Box<Node>&)> strong_connect = [&](const Box<Node>& v) {
				ctx.index[v] = ctx.low_link[v] = ctx.counter++;
				ctx.stack.push(v);
				ctx.on_stack[v] = true;

				if (auto it = graph.connections.find(v); it != graph.connections.end()) {
					for (const auto& w : it->second) {
						if (!in_subgraph.count(w)) {
							// Skip nodes pruned in phase 2
							continue;
						}

						if (!ctx.index.count(w)) {
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
						if (w == v) break;
					}
					ctx.SCCs.emplace_back(std::move(scc));
				}
			};

			for (auto& variant : g) {
				const auto& node = std::get<Box<Node>>(variant);
				if (!ctx.index.count(node)) {
					strong_connect(node);
				}
			}

			// SCCs is in reverse order so
			std::vector<std::variant<Box<Node>, NodeCluster>> sorted_g;
			sorted_g.reserve(ctx.SCCs.size());

			for (auto& scc : ctx.SCCs) {
				if (scc.size() == 1) {
					sorted_g.emplace_back(scc[0]);
				} else {
					TOAST_TRACE("World", "Dependency cycle detected ({} nodes) -> grouping into NodeCluster", scc.size());
					sorted_g.emplaceBack(NodeCluster(scc));
				}
			}
			result_subraphs.emplace_back(std::move(sorted_g));
		}

		subgraphs = std::move(result_subgraphs);

		// Phase 4: Wave algorithm
		// Assigns a wave number to each item
		// Items sharing a wave have no dependency on each other and can be ticked in parallel via futures

		// Map every node back to its containing item so NodeClusters are treated as a single scheduling unit
		struct ItemRef {
			int subgraph;
			int index;
		};

		std::unordered_map<Box<Node>, ItemRef> node_to_item;

		for (int si = 0; si < (int)subgraphs.size(); ++si) {
			for (int ii = 0; ii < (int)subgraphs[si].size(); ++ii) {
				std::visit(
				    [&](auto& item) {
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
		for (int si = 0; si < (int)subgraphs.size(); ++si) {
			waves[si].assign(subgraphs[si].size(), 0);
		}

		for (int si = 0; si < (int)subgraphs.size(); ++si) {
			// Reverse iteration vecause of tarjan
			for (int ii = (int)subgraphs[si].size() - 1; ii >= 0; --ii) {
				// Collect all physical nodes belonging to this item
				std::vector<Box<Node>> item_nodes;
				std::visit(
				    [&](auto& item) {
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
		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> bucket(max_wave + 1);

		for (int si = 0; si < (int)subgraphs.size(); ++si) {
			for (int ii = 0; ii < (int)subgraphs[si].size(); ++ii) {
				bucket[waves[si][ii]].emplace_back(std::move(subgraphs[si][ii]));
			}
		}

		// Phase 5: Bucket optimization
		// We are going to create 4 equal buckets representing each of the tick functions and then
		// search for all the nodes that do not contain those functions and erase them from the list
		TickSchedule ts = {
			.early_tick = bucket,
			.tick = bucket,
			.post_physics = bucket,
			.late_tick = bucket,
		};

		for (auto& wave : ts.early_tick) {
			std::erase_if(wave, [](std::variant<Box<Node>, NodeCluster>& item) -> bool {
				if (std::holds_alternative<Box<Node>>(item)) {
					auto node = std::get<Box<Node>>(item);
					return node->table->table.early_tick.empty();
				}

				auto cluster = std::get<NodeCluster>(item);
				bool has_function = false;
				for (const auto& node : cluster.nodes) {
					if (not node->table->table.early_tick.empty()) return true;
				}
				return !has_function;
			});
		}

		for (auto& wave : ts.tick) {
			std::erase_if(wave, [](std::variant<Box<Node>, NodeCluster>& item) -> bool {
				if (std::holds_alternative<Box<Node>>(item)) {
					auto node = std::get<Box<Node>>(item);
					return node->table->table.tick.empty();
				}

				auto cluster = std::get<NodeCluster>(item);
				bool has_function = false;
				for (const auto& node : cluster.nodes) {
					if (not node->table->table.tick.empty()) return true;
				}
				return !has_function;
			});
		}

		for (auto& wave : ts.post_physics) {
			std::erase_if(wave, [](std::variant<Box<Node>, NodeCluster>& item) -> bool {
				if (std::holds_alternative<Box<Node>>(item)) {
					auto node = std::get<Box<Node>>(item);
					return node->table->table.post_physics.empty();
				}

				auto cluster = std::get<NodeCluster>(item);
				bool has_function = false;
				for (const auto& node : cluster.nodes) {
					if (not node->table->table.post_physics.empty()) return true;
				}
				return !has_function;
			});
		}

		for (auto& wave : ts.late_tick) {
			std::erase_if(wave, [](std::variant<Box<Node>, NodeCluster>& item) -> bool {
				if (std::holds_alternative<Box<Node>>(item)) {
					auto node = std::get<Box<Node>>(item);
					return node->table->table.late_tick.empty();
				}

				auto cluster = std::get<NodeCluster>(item);
				bool has_function = false;
				for (const auto& node : cluster.nodes) {
					if (not node->table->table.late_tick.empty()) return true;
				}
				return !has_function;
			});
		}

		tick_schedule = std::move(ts);

	}

	void swapRoot(Node* node);
	void addGlobal();
	void removeGlobal();
	void addCached();
	void removeCached();

private:
	inline static World* instance = nullptr;

	/**
	 * @brief Creates a node and stores it in memory
	 *
	 * This function is implementation only and the result is an uninitialized node;
	 * it is used for detail implementation of the world only
	 */
	auto nodeAllocation() noexcept -> Box<Node>;

	struct {
		event::Listener listener;

		std::unordered_set<_detail::ControlBox> nodes;
		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> tick_schedule;

		Box<Node> root_node;
		std::vector<Box<Node>> global_nodes;
		std::vector<Box<Node>> cached_nodes;
		std::vector<Box<Node>> load_queue;
		std::vector<Box<Node>> destroy_queue;

		std::thread load_thread;
	} m;

	struct TickSchedule {
		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> early_tick;
		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> tick;
		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> post_physics;
		std::vector<std::vector<std::variant<Box<Node>, NodeCluster>>> late_tick;
	} tick_schedule;

	struct DependencyGraph {
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> connections;
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> inverse_connections;
	} graph;
};

}
