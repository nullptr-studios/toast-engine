/**
 * @file World.hpp
 * @author Xein
 * @date 18 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
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

		Box<Node> root_node;
		std::vector<Box<Node>> global_nodes;
		std::vector<Box<Node>> cached_nodes;
		std::vector<Box<Node>> load_queue;
		std::vector<Box<Node>> destroy_queue;

		std::thread load_thread;
	} m;

	struct DependencyGraph {
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> connections;
		std::unordered_map<Box<Node>, std::vector<Box<Node>>> inverse_connections;
	} graph;
};

}
