#include "node_owner.hpp"

#include "node.hpp"

namespace toast {

auto NodeOwner::nodeAllocation(std::optional<assets::Prefab::BasicNode> node_data) noexcept -> Box<Node> {
	ZoneScoped;

	// Identify the type and get reflection info
	std::string type = node_data.has_value() ? node_data->type : "toast::Node";
	const NodeInfo* info = NodeRegistry::reflect(type);

#ifndef NDEBUG
	if (!info) {
		TOAST_WARN("World", "Reflection information for type {} not found. Falling back to toast::Node", type);
		info = NodeRegistry::reflect("toast::Node");
	}
#endif

	// Node allocation
#ifdef NDEBUG
	Node* raw_node = info->construct();
#else
	Node* raw_node = (info && info->construct) ? info->construct() : new Node();
#endif

	{
		std::scoped_lock lock(nodes_mutex);
		auto [it, result] = nodes.emplace(raw_node);
		TOAST_ASSERT(result, "World", "Node allocation failed");
	}
	raw_node->m_info = info;     // attach reflection data
	raw_node->m_owner = this;    // attach owner ptr

	if (node_data.has_value()) {
		raw_node->name(node_data->name);

		if (info) {
			info->forEachBaseType([&](const NodeInfo& level) {
				for (const auto& f : level.all_fields) {
					auto f_data = node_data->find(f.name);
					if (not f_data.has_value()) {
						continue;
					}

					// Read only attributes shouldn't be serialized
					if (f.hasAttribute("ReadOnly")) {
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

auto NodeOwner::buildTree(std::vector<Box<Node>>&& nodes, const assets::AssetHandle<assets::Prefab>& file) -> Box<Node> {
	ZoneScoped;

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

	if (root.exists()) {
		TOAST_TRACE("World", "Built tree {} ({}) with {} nodes", root->name(), root->uid(), nodes.size());
	}
	return root;
}

void NodeOwner::releaseNode(_detail::ControlBox& control) noexcept {
	control.node = nullptr;
	tombstones++;
}

void NodeOwner::reapTombstones() noexcept {
	if (tombstones == 0) {
		return;
	}
	std::scoped_lock lock(nodes_mutex);
	tombstones -= std::erase_if(nodes, [](const _detail::ControlBox& control) {
		return control.node == nullptr && control.ref_count.load(std::memory_order_acquire) == 0;
	});
}

}
