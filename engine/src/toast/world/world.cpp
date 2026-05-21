#include "world.hpp"

#include <sstream>
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
