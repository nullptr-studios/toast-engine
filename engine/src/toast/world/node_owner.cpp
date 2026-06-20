#include "node_owner.hpp"

#include "node.hpp"
#include "toast/log.hpp"
#include "toast/world/workspace_events.hpp"

#include <algorithm>
#include <future>
#include <memory>
#include <toast/assets/assets.hpp>
#include <toast/thread_pool.hpp>
#include <tracy/Tracy.hpp>

namespace toast {

namespace {
auto referenceUid(const assets::Prefab::BasicNode& chunk) -> uint64_t {
	if (auto field = chunk.find("m_source_prefab")) {
		try {
			return field->as<toast::UID>().data();
		} catch (const std::bad_any_cast& e) { TOAST_ERROR("World", "Bad cast at {}: {}", chunk.name, e.what()); }
	}
	return 0;
}
}

auto INodeOwner::requestRuntimeCreate(Node& parent, std::string_view type) -> Box<Node> {
	ZoneScoped;

	// Allocation
	Box node = this->nodeAllocation(type);
	node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	// Data structure generation
	generateUid(node);
	node->m_parent = parent;
	parent.m_children.emplace_back(node);
	node->m_state = parent.m_state;
	node->m_type = NodeType::child;
	node->m_inherited_enabled = parent.enabled();
	node->m_name = uniqueChildName(parent, stripNamespace(node->m_info->type));

	// Initialization
	node->callTick(node->info(), TickFunctionList::init);
	node->callTick(node->info(), TickFunctionList::begin);
	node->enabled(true);

	event::send<event::RequestHierarchyUpdate>();
	TOAST_TRACE("World", "Spawned node in {}", parent.name());
	return node;
}

auto INodeOwner::requestRuntimeSpawn(Node& parent, UID uid) -> Box<Node> {
	ZoneScoped;

	// Obtain asset
	auto file = assets::load<assets::Prefab>(uid);
	if (not file.hasValue()) {
		TOAST_ERROR("World", "Couldn't load prefab {} to spawn", uid);
		return {};
	}

	// Allocation
	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](UID id) { return assets::load<assets::Prefab>(id); };
	Box<Node> root = this->instantiate(file, ctx);
	if (not root.exists()) {
		TOAST_ERROR("World", "Failed to instantiate prefab {} to spawn", uid);
		return {};
	}

	// Data structure generation
	root->m_uid.generate();
	root->m_parent = parent;
	parent.m_children.emplace_back(root);
	root->m_state = parent.m_state;
	root->m_type = NodeType::root;
	root->m_inherited_enabled = parent.enabled();

	// Initialization
	root->propagateCallTick(root->info(), TickFunctionList::init);
	root->propagateCallTick(root->info(), TickFunctionList::begin);
	root->enabled(true);

	event::send<event::RequestHierarchyUpdate>();
	TOAST_TRACE("World", "Spawned node {} in {}", root->name(), parent.name());
	return root;
}

auto INodeOwner::requestRuntimeSpawn(Node& parent, std::string_view uri) -> Box<Node> {
	auto uid = assets::resolveURI(uri);
	if (not uid.has_value()) {
		TOAST_WARN("World", "Couldn't find file {} to do runtime spawn", uri);
		return {};
	}

	return requestRuntimeSpawn(parent, *uid);
}

void INodeOwner::generateUid(Node& node) {
	node.m_uid.generate();
}

auto INodeOwner::stripNamespace(std::string_view type) -> std::string_view {
	auto pos = type.rfind("::");
	return pos == std::string_view::npos ? type : type.substr(pos + 2);
}

auto INodeOwner::uniqueChildName(const Node& parent, std::string_view base) -> std::string {
	auto taken = [&](std::string_view candidate) {
		return std::ranges::any_of(parent.children(), [&](const Box<Node>& c) { return c->name() == candidate; });
	};

	std::string candidate {base};
	for (int n = 2; taken(candidate); ++n) {
		candidate = std::string {base} + ' ' + std::to_string(n);
	}
	return candidate;
}

auto INodeOwner::nodeAllocation(std::string_view type) noexcept -> Box<Node> {
	ZoneScoped;

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

	return raw_node->box();
}

auto INodeOwner::nodeAllocation(const assets::Prefab::BasicNode& node_data) noexcept -> Box<Node> {
	std::string type = node_data.type;
	auto box = nodeAllocation(type);
	applyFields(*box, node_data);
	return box;
}

void INodeOwner::applyFields(Node& node, const assets::Prefab::BasicNode& data) {
	ZoneScoped;

	node.name(data.name);

	const NodeInfo* info = node.info();
	if (not info) {
		return;
	}

	info->forEachBaseType([&](const NodeInfo& level) {
		for (const auto& f : level.all_fields) {
			if (f.name == "m_source_prefab") {
				continue;
			}

			auto f_data = data.find(f.name);
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

			f.set(&node, f_data->value);
		}
	});
}

auto INodeOwner::buildTree(std::vector<Box<Node>>&& nodes, const assets::AssetHandle<assets::Prefab>& file) -> Box<Node> {
	ZoneScoped;

	std::unordered_map<uint64_t, Box<Node>> uid_map;
	uid_map.reserve(nodes.size());
	for (auto& node : nodes) {
		auto [it, inserted] = uid_map.emplace(node->uid().data(), node);
#ifndef NDEBUG
		if (not inserted) {
			TOAST_WARN(
			    "World",
			    "Duplicate UID {} within a single prefab ({} and {}); keeping the first",
			    node->uid(),
			    it->second->name(),
			    node->name()
			);
		}
#endif
	}

	Box<Node> root;

	// Prefab serialization guarantees exactly one rootless node, written first
	for (size_t i = 0; i < nodes.size(); ++i) {
		auto& node = nodes[i];
		const auto& data = file->nodes[i];

		auto parent_field = data.find("m_parent");
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

auto INodeOwner::instantiate(const assets::AssetHandle<assets::Prefab>& file, InstantiateContext& ctx) -> Box<Node> {
	ZoneScoped;

	if (not file.hasValue()) {
		TOAST_ERROR("World", "Cannot instantiate an unresolved prefab {}", file.uid());
		return {};
	}

	ctx.asset_chain.push_back(file.uid().data());

	// deserialize + run the pre-tick lifecycle, then mark as loading
	auto alloc_leaf = [this](const assets::Prefab::BasicNode& chunk) -> Box<Node> {
		Box<Node> node = nodeAllocation(chunk);
		node->callTick(node->info(), TickFunctionList::load);
		node->callTick(node->info(), TickFunctionList::pre_init);    // TODO: deprecate pre_init
		node->m_state = NodeState::loading;
		return node;
	};

	auto make_unresolved = [&](const assets::Prefab::BasicNode& chunk, uint64_t ref_uid) -> Box<Node> {
		Box<Node> node = alloc_leaf(chunk);
		node->m_unresolved_chunk = std::make_shared<const assets::Prefab::BasicNode>(chunk);
		node->m_source_prefab = assets::AssetHandle<assets::Prefab>(nullptr, toast::UID(ref_uid));
		return node;
	};

	std::vector<Box<Node>> slots(file->nodes.size());
	std::vector<std::pair<size_t, std::future<Box<Node>>>> pending;

	for (size_t i = 0; i < file->nodes.size(); ++i) {
		const assets::Prefab::BasicNode* chunk = &file->nodes[i];
		uint64_t ref_uid = referenceUid(*chunk);

		if (ref_uid == 0) {
			// allocate the leaf on the thread pool
			pending.emplace_back(i, ThreadPool::push([&alloc_leaf, chunk]() { return alloc_leaf(*chunk); }));
			continue;
		}

		// expand a nested instance synchronously on this thread
		bool cycle = std::ranges::find(ctx.asset_chain, ref_uid) != ctx.asset_chain.end();
		assets::AssetHandle<assets::Prefab> sub = cycle ? assets::AssetHandle<assets::Prefab> {} : ctx.resolver(toast::UID(ref_uid));

		if (cycle or not sub.hasValue()) {
			TOAST_ERROR(
			    "World",
			    "Could not instantiate nested prefab {} ({}); keeping reference unresolved",
			    toast::UID(ref_uid),
			    cycle ? "cycle detected" : "asset missing"
			);
			slots[i] = make_unresolved(*chunk, ref_uid);
			continue;
		}

		Box<Node> sub_root = instantiate(sub, ctx);
		if (not sub_root.exists()) {
			slots[i] = make_unresolved(*chunk, ref_uid);
			continue;
		}

		applyFields(*sub_root, *chunk);

		// Everything below an instance root is interior to that instance
		auto mark_interior = [](this auto&& self, Node& n) -> void {
			for (auto& child : n.m_children) {
				child->m_prefab_interior = true;
				self(*child);
			}
		};
		mark_interior(*sub_root);

		slots[i] = sub_root;
	}

	for (auto& [index, fut] : pending) {
		slots[index] = fut.get();
	}

	Box<Node> root = buildTree(std::move(slots), file);

	if (root.exists() && root->m_source_prefab.uid().data() == 0) {
		root->m_source_prefab = file;
	}

	ctx.asset_chain.pop_back();
	return root;
}

void INodeOwner::releaseNode(_detail::ControlBox& control) noexcept {
	control.node = nullptr;
	tombstones++;
}

void INodeOwner::reapTombstones() noexcept {
	if (tombstones == 0) {
		return;
	}
	std::scoped_lock lock(nodes_mutex);
	tombstones -= std::erase_if(nodes, [](const _detail::ControlBox& control) {
		return control.node == nullptr && control.ref_count.load(std::memory_order_acquire) == 0;
	});
}

}
