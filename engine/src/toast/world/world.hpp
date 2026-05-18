/**
 * @file World.hpp
 * @author Xein
 * @date 18 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "world_events.hpp"
#include <toast/events/listener.hpp>
#include "node.hpp"

namespace toast {

class Node;

class World {
public:
	World() = default;
	~World() = default;

	auto requestRuntimeCreation(Node& parent) -> Node* {
		ZoneScoped;

		// Phase 1: preinitialization
		auto* node = controlBlockAllocation();
		node->table->load(node);
		node->table->preInit(node);

		// Phase 2: data structure generation
		node->m.uuid.generate();
		node->m.parent = &parent;
		parent.m.children.emplace_back(node);
		// TODO: dependency graph

		// Phase 3: node initialization
		node->table->init(node);
		return node;
	}

	void swapRoot(Node* node) {
		switch (node->m.state) {
			case NodeState::cached: {
				auto it = std::ranges::find(m.cached_nodes, node);
				if (it == m.cached_nodes.end()) goto ERROR_MISSING;
				if (m.root_node != nullptr) m.cached_nodes.emplace_back(m.root_node);
				m.root_node = node;
				std::erase(m.cached_nodes, node);
				break;
			}
			case NodeState::global: {
				auto it = std::ranges::find(m.global_nodes, node);
				if (it == m.global_nodes.end()) goto ERROR_MISSING;
				if (m.root_node != nullptr) m.global_nodes.emplace_back(m.root_node);
				m.root_node = node;
				std::erase(m.global_nodes, node);
				break;
			}
			case NodeState::root: {
				if (node != m.root_node) goto ERROR_MISSING;
				TOAST_WARN("World", "Trying to swap root node with itself");
				break;
			}
			default: {
				TOAST_ERROR("World", "Trying to swap a node that is not initialized yet or queued for destruction");
			}
		}
		return;

ERROR_MISSING:
		TOAST_WARN("World", "Trying to swap root node with {} which is not a top-level node (parent: {})", node->name(), node->parent()->name());
	}

private:
	/**
	 * @brief Creates a node and stores it in memory
	 *
	 * This function is implementation only and the result is an uninitialized node;
	 * it is used for detail implementation of the world only
	 */
	auto controlBlockAllocation() noexcept -> Node* {
		//
	}

	struct {
		event::Listener listener;
		Node* root_node = nullptr;
		std::vector<Node*> global_nodes;
		std::vector<Node*> cached_nodes;
		std::vector<Node*> load_queue;
		std::vector<Node*> destroy_queue;
	} m;
};

}
