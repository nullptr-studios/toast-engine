#include "world.hpp"

#include <toast/thread_pool.hpp>

namespace toast {

auto World::nodeAllocation() noexcept -> Box<Node> {
	ZoneScoped;
	auto [it, result] = m.nodes.emplace(new Node());
	TOAST_ASSERT(result, "World", "Node allocation failed");
	return it->node;
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

void World::swapRoot(Node* node) {
	switch (node->m.state) {
		case NodeState::cached: {
			auto it = std::ranges::find(m.cached_nodes, node);
			if (it == m.cached_nodes.end()) {
				goto ERROR_MISSING;
			}
			if (m.root_node.exists()) {
				m.cached_nodes.emplace_back(m.root_node);
			}
			m.root_node = node;
			std::erase(m.cached_nodes, node);
			break;
		}
		case NodeState::global: {
			auto it = std::ranges::find(m.global_nodes, node);
			if (it == m.global_nodes.end()) {
				goto ERROR_MISSING;
			}
			if (m.root_node.exists()) {
				m.global_nodes.emplace_back(m.root_node);
			}
			m.root_node = node;
			std::erase(m.global_nodes, node);
			break;
		}
		case NodeState::root: {
			if (node != m.root_node) {
				goto ERROR_MISSING;
			}
			TOAST_WARN("World", "Trying to swap root node with itself");
			break;
		}
		default: {
			TOAST_ERROR("World", "Trying to swap a node that is not initialized yet or queued for destruction");
		}
	}
	return;

ERROR_MISSING:
	TOAST_WARN(
	    "World", "Trying to swap root node with {} which is not a top-level node (parent: {})", node->name(), node->parent()->name()
	);
}

}
