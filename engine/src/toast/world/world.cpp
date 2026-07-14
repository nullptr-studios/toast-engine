#include "world.hpp"

#include "node_3d.hpp"
#include "workspace_events.hpp"
#include "world_test_access.hpp"

#include <chrono>
#include <sstream>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/types.hpp>
#include <toast/thread_pool.hpp>
#include <toast/uri_handler.hpp>
#include <utility>

namespace toast {

using namespace _detail;

World::World() {
	instance = this;

	m.listener.subscribe<event::LoadNode>("load_node", [](event::LoadNode& e) {
		loadNode(e.uid);
		return false;
	});

	m.listener.subscribe<event::LoadNodeByURI>("load_node_uri", [](event::LoadNodeByURI& e) {
		loadNode(e.uri);
		return false;
	});

	m.listener.subscribe<event::SetWorldRoot>("set_world_root", [](event::SetWorldRoot& e) {
		setRoot(*e.node);
		return false;
	});

	m.listener.subscribe<event::CacheNode>("cache_node", [](event::CacheNode& e) {
		cacheNode(*e.node);
		return false;
	});

	m.listener.subscribe<event::DestroyNode>("destroy_node", [](event::DestroyNode& e) {
		destroyNode(*e.node);
		return false;
	});

	m.listener.subscribe<event::MakeNodeGlobal>("make_node_global", [](event::MakeNodeGlobal& e) {
		instance->moveToGlobal(*e.node);
		return false;
	});

	m.listener.subscribe<event::AttachNode>("attach_node", [](event::AttachNode& e) {
		instance->moveToChild(*e.node, *e.parent);
		return false;
	});

	TOAST_INFO("World", "Created world");
}

World::~World() {
	ZoneScoped;
	for (auto& f : m.load_futures) {
		if (f.valid()) {
			f.wait();
		}
	}

	TOAST_INFO("World", "Destroyed world");
}

void World::tick() {
	ZoneScoped;

	drainDestroyQueue();
	drainLoadQueue();
	drainSpawnQueue();

	m_scheduler.run();
}

void World::registerDependency(Node& from, Node& to) {
	instance->m_scheduler.registerDependency(from, to);
}

void World::unregisterDependency(Node& from, Node& to) {
	instance->m_scheduler.unregisterDependency(from, to);
}

void World::loadNode(UID uid, bool activate_as_root) {
	ZoneScoped;
	ZoneNameF("World::loadNode(%s)", uid.get().c_str());
	TOAST_INFO("World", "Loading node {} from file", uid);

	if (activate_as_root) {
		std::scoped_lock lock(instance->m.load_mutex);
		instance->m.pending_root_uid = uid;
	}

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
#ifdef TRACY_ENABLE
		tracy::SetThreadName("World::loadNode worker");
#endif
		ZoneScoped;    // NOLINT
		ZoneNameF("World::loadNode(%s)::async", uid.get().c_str());

		auto node_file = assets::load<assets::Prefab>(uid);
		if (not node_file.hasValue()) {
			TOAST_ERROR("World", "Couldn't load Node {}", uid);
			return;
		}

		INodeOwner::InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> root = instance->instantiate(node_file, ctx);

		if (not root.exists()) {
			TOAST_ERROR("World", "Failed to instantiate Node {}", uid);
			return;
		}

		root->propagateCallTick(root->info(), TickFunctionList::init);
		TOAST_TRACE("World", "Node {} ({}) finished loading", root->name(), root->uid());

		std::scoped_lock lock(instance->m.load_mutex);
		instance->trees.load_queue.emplace_back(std::move(root));
	});

	std::scoped_lock lock(instance->m.load_mutex);
	std::erase_if(instance->m.load_futures, [](std::future<void>& f) {
		return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	});
	instance->m.load_futures.emplace_back(std::move(future));
}

void World::loadNode(std::string_view uri, bool activate_as_root) {
	// just reroute to the actual loadNode() implementation
	auto id = assets::resolveURI(uri);

#ifndef NDEBUG
	if (not id.has_value()) {
		TOAST_WARN("World", "Couldn't load Node {}", uri);
		return;
	}
#endif

	loadNode(*id, activate_as_root);
}

void World::drainLoadQueue() {
	std::vector<Box<Node>> loaded;
	UID pending_uid {0};
	{
		std::scoped_lock lock(m.load_mutex);
		if (trees.load_queue.empty()) {
			return;
		}
		std::swap(loaded, trees.load_queue);
		pending_uid = m.pending_root_uid;
	}

	ZoneScoped;

	// Freshly loaded trees go to the cached list and are ready to be activated
	for (auto& root : loaded) {
		const UID node_uid = root->uid();
		root->changeNodeState(NodeState::cached);
		TOAST_TRACE("World", "Node {} ({}) moved to cache", root->name(), node_uid);
		trees.cached.emplace_back(std::move(root));

		// Auto-activate if this is the pending start scene
		if (pending_uid.data() != 0 && node_uid.data() == pending_uid.data()) {
			TOAST_INFO("World", "Auto-activating start scene {}", node_uid);
			{
				std::scoped_lock lock(m.load_mutex);
				m.pending_root_uid = UID {0};
			}
			setRoot(*trees.cached.back());
		}
	}
}

void World::spawn(UID prefab, Node& parent) {
	ZoneScoped;
	TOAST_INFO("World", "Spawning prefab {} under {}", prefab, parent.name());

	Box<Node> parent_box = parent.box();
	auto future = std::async(std::launch::async, [prefab, parent_box]() {
#ifdef TRACY_ENABLE
		tracy::SetThreadName("World::spawn worker");
#endif
		ZoneScoped;    // NOLINT

		auto file = assets::load<assets::Prefab>(prefab);
		if (not file.hasValue()) {
			TOAST_ERROR("World", "Couldn't load prefab {} to spawn", prefab);
			return;
		}

		INodeOwner::InstantiateContext ctx;
		ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
		Box<Node> root = instance->instantiate(file, ctx);
		if (not root.exists()) {
			TOAST_ERROR("World", "Failed to instantiate prefab {} to spawn", prefab);
			return;
		}

		// spawned instances need a unique UID within the parent's namespace
		// even if two copies of the same prefab are spawned concurrently
		generateUid(*root);
		root->propagateCallTick(root->info(), TickFunctionList::init);
		root->changeNodeState(NodeState::cached);

		std::scoped_lock lock(instance->m.load_mutex);
		instance->m.spawn_queue.emplace_back(std::move(root), parent_box);
	});

	std::scoped_lock lock(instance->m.load_mutex);
	std::erase_if(instance->m.load_futures, [](std::future<void>& f) {
		return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	});
	instance->m.load_futures.emplace_back(std::move(future));
}

void World::drainSpawnQueue() {
	std::vector<std::pair<Box<Node>, Box<Node>>> ready;
	{
		std::scoped_lock lock(m.load_mutex);
		if (m.spawn_queue.empty()) {
			return;
		}
		std::swap(ready, m.spawn_queue);
	}

	ZoneScoped;

	for (auto& [root, parent] : ready) {
		if (not parent.exists() || parent->m_state == NodeState::destroy) {
			TOAST_WARN("World", "Spawn target for {} no longer exists; dropping the spawn", root->name());
			continue;
		}

		// Keep the placement UID unique within the parents namespace
		while (findNode(root->uid(), &*parent).exists()) {
			generateUid(*root);
		}

		moveToChild(*root, *parent);
	}
}

auto World::findNode(const UID& uid, Node* scope) -> Box<Node> {
	ZoneScoped;

	Node* start = scope;
	if (not start) {
		if (not instance->trees.root.exists()) {
			return {};
		}
		start = &*instance->trees.root;
	}

	// Depth-first, but a nested instance root is opaque
	auto dfs = [&uid](this auto&& self, Node& n, bool is_scope_root) -> Box<Node> {
		if (n.uid().data() == uid.data()) {
			return n.box();
		}
		if (not is_scope_root && n.isInstanceRoot()) {
			return {};
		}
		for (auto& child : n.m_children) {
			if (auto found = self(*child, false)) {
				return found;
			}
		}
		return {};
	};

	return dfs(*start, true);
}

auto World::findNode(std::string_view path) -> Box<Node> {
	Box<Node> current;
	Node* scope = nullptr;    // first segment is resolved from the world root
	size_t start = 0;

	while (true) {
		size_t slash = path.find('/', start);
		std::string_view seg = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
		if (seg.empty()) {
			return {};
		}

		current = findNode(UID(UID::fromString(seg)), scope);
		if (not current.exists()) {
			return {};
		}
		scope = &*current;    // the next segment is scoped to this instance's interior

		if (slash == std::string_view::npos) {
			break;
		}
		start = slash + 1;
	}

	return current;
}

auto World::uidPath(const Node& node) -> std::string {
	const Node* root = instance->trees.root.exists() ? &*instance->trees.root : nullptr;

	std::vector<std::string> parts;
	for (const Node* n = &node; n != nullptr;) {
		if (n->isInstanceRoot()) {
			parts.push_back(n->uid().get());
		}
		if (n == root) {
			break;
		}
		Box<Node> parent = n->parentInternal();
		n = parent.exists() ? &*parent : nullptr;
	}
	std::ranges::reverse(parts);

	std::string result;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i != 0) {
			result += '/';
		}
		result += parts[i];
	}
	return result;
}

namespace {

auto looksLikeUid(std::string_view seg) -> bool {
	if (seg.size() != 11) {
		return false;
	}
	for (char c : seg) {
		bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		if (not ok) {
			return false;
		}
	}
	return true;
}

enum class QueryRoot : uint8_t {
	self,
	prefab_root,
	world_root,
	global
};

struct ParsedQuery {
	QueryRoot root = QueryRoot::self;
	std::vector<std::string_view> segments;    // path after the namespace keyword
	bool valid = true;
};

auto parseQuery(std::string_view q) -> ParsedQuery {
	ParsedQuery out;

	// only node:// is accepted, any other URI scheme (asset://) is an error
	if (auto pos = q.find("://"); pos != std::string_view::npos) {
		if (q.substr(0, pos) != "node") {
			out.valid = false;
			return out;
		}
		q.remove_prefix(pos + 3);
	}

	std::vector<std::string_view> parts;
	size_t start = 0;
	while (start <= q.size()) {
		size_t slash = q.find('/', start);
		size_t len = (slash == std::string_view::npos) ? std::string_view::npos : slash - start;
		std::string_view seg = q.substr(start, len);
		if (not seg.empty()) {
			parts.push_back(seg);
		}
		if (slash == std::string_view::npos) {
			break;
		}
		start = slash + 1;
	}

	if (parts.empty()) {
		out.valid = false;
		return out;
	}

	size_t first = 0;
	if (parts[0] == "root") {
		out.root = QueryRoot::prefab_root;
		first = 1;
	} else if (parts[0] == "world_root") {
		out.root = QueryRoot::world_root;
		first = 1;
	} else if (parts[0] == "global") {
		out.root = QueryRoot::global;
		first = 1;
	}

	out.segments.assign(parts.begin() + first, parts.end());
	return out;
}

}

auto World::findScoped(Node& scope, std::string_view seg, bool by_uid) -> Box<Node> {
	uint64_t target = by_uid ? UID::fromString(seg) : 0;

	auto dfs = [&](this auto&& self, Node& n, bool is_scope_root) -> Box<Node> {
		bool match = by_uid ? (n.uid().data() == target) : (n.name() == seg);
		if (match) {
			return n.box();
		}
		// Instances are opaque
		if (not is_scope_root && n.isInstanceRoot()) {
			return {};
		}
		for (auto& child : n.m_children) {
			if (auto found = self(*child, false)) {
				return found;
			}
		}
		return {};
	};

	return dfs(scope, true);
}

void World::searchScoped(Node& scope, std::string_view seg, bool by_uid, std::vector<Box<Node>>& out) {
	uint64_t target = by_uid ? UID::fromString(seg) : 0;

	auto dfs = [&](this auto&& self, Node& n) -> void {
		if (by_uid ? (n.uid().data() == target) : (n.name() == seg)) {
			out.emplace_back(n.box());
		}
		// search() is the general form, it crosses into prefab interiors
		for (auto& child : n.m_children) {
			self(*child);
		}
	};

	dfs(scope);
}

auto World::findFrom(const Node& origin, std::string_view query) -> Box<Node> {
	ZoneScoped;

	ParsedQuery pq = parseQuery(query);
	if (not pq.valid) {
		TOAST_ERROR("World", "Invalid node query \"{}\"", query);
		return {};
	}

	// Resolve the starting scope from the namespace keyword
	std::vector<Node*> origins;
	switch (pq.root) {
		case QueryRoot::self: origins.push_back(const_cast<Node*>(&origin)); break;
		case QueryRoot::prefab_root: origins.push_back(&*const_cast<Node&>(origin).root()); break;
		case QueryRoot::world_root:
			if (trees.root.exists()) {
				origins.push_back(&*trees.root);
			}
			break;
		case QueryRoot::global:
			if (pq.segments.empty()) {
				TOAST_ERROR("World", "find(\"global\") is ambiguous; use search(\"global\") to list global nodes");
				return {};
			}
			for (auto& g : trees.global) {
				origins.push_back(&*g);
			}
			break;
	}

	// A bare keyword resolves to the scope node itself
	if (pq.segments.empty()) {
		return origins.empty() ? Box<Node> {} : origins.front()->box();
	}

	bool by_uid = looksLikeUid(pq.segments.front());

	for (Node* start : origins) {
		Node* scope = start;
		Box<Node> current;
		bool ok = true;
		for (std::string_view seg : pq.segments) {
			current = findScoped(*scope, seg, by_uid);
			if (not current.exists()) {
				ok = false;
				break;
			}
			scope = &*current;
		}
		if (ok) {
			return current;    // find() returns the first match
		}
	}

	return {};
}

auto World::searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> {
	ZoneScoped;

	ParsedQuery pq = parseQuery(query);
	if (not pq.valid) {
		TOAST_ERROR("World", "Invalid node query \"{}\"", query);
		return {};
	}

	std::vector<Box<Node>> out;

	// "global" with no path lists the world's global nodes
	if (pq.root == QueryRoot::global && pq.segments.empty()) {
		out.reserve(trees.global.size());
		for (auto& g : trees.global) {
			out.emplace_back(g);
		}
		return out;
	}

	std::vector<Node*> origins;
	switch (pq.root) {
		case QueryRoot::self: origins.push_back(const_cast<Node*>(&origin)); break;
		case QueryRoot::prefab_root: origins.push_back(&*const_cast<Node&>(origin).root()); break;
		case QueryRoot::world_root:
			if (trees.root.exists()) {
				origins.push_back(&*trees.root);
			}
			break;
		case QueryRoot::global:
			for (auto& g : trees.global) {
				origins.push_back(&*g);
			}
			break;
	}

	if (pq.segments.empty()) {
		for (Node* o : origins) {
			out.emplace_back(o->box());
		}
		return out;
	}

	bool by_uid = looksLikeUid(pq.segments.front());

	for (Node* start : origins) {
		Node* scope = start;
		bool ok = true;
		for (size_t i = 0; i + 1 < pq.segments.size(); ++i) {
			Box<Node> step = findScoped(*scope, pq.segments[i], by_uid);
			if (not step.exists()) {
				ok = false;
				break;
			}
			scope = &*step;
		}
		if (ok) {
			searchScoped(*scope, pq.segments.back(), by_uid, out);
		}
	}

	return out;
}

auto World::setRoot(Node& node) -> Box<Node> {
	return instance->swapRoot(node);
}

auto World::cacheNode(Node& node) -> Box<Node> {
	return instance->moveToCached(node);
}

auto World::findCached(std::string_view name) -> Box<Node> {
	ZoneScoped;

	for (const auto& node : instance->trees.cached) {
		if (node->name() == name) {
			return node;
		}
	}

	TOAST_TRACE("World", "Node {} not found in cached", name);
	return {};
}

void World::hotReload() {
	if (!instance) {
		return;
	}

	std::function<void(const Box<Node>&)> refresh_fn;
	refresh_fn = [&refresh_fn](const Box<Node>& box) -> void {
		if (!box.exists()) {
			return;
		}
		const_cast<Node&>(*box).refreshInfo();
		for (const auto& child : box->children()) {
			refresh_fn(child);
		}
	};

	if (instance->trees.root.exists()) {
		refresh_fn(instance->trees.root);
	}
	for (const auto& node : instance->trees.cached) {
		const_cast<Node&>(*node).refreshInfo();
	}
}

void World::hotReloadScripts(toast::UID script_uid) {
	if (!instance) {
		return;
	}
	instance->reloadScriptsUsing(script_uid);
	// a reload can add or remove Lua tick functions, so the schedule needs recompute
	instance->computeDependencyGraph();
}

auto World::graphviz() -> std::string {
	return instance->dependencyGraphGraphviz();
}

void World::destroyNode(Node& node) {
	ZoneScoped;
	ZoneNameF("World::destroyNode(%s)", node.name().data());

	if (node.m_state != NodeState::cached) {
		TOAST_WARN("World", "Only cached nodes can be destroyed, {} ({}) is still in use", node.name(), node.uid());
		return;
	}

	std::erase(instance->trees.cached, node.box());
	node.changeNodeState(NodeState::destroy);
	node.enabled(false);
	node.propagateCallTick(node.info(), TickFunctionList::destroy);
	instance->trees.destroy_queue.emplace_back(node.box());
	TOAST_TRACE("World", "Queued node {} ({}) for destruction", node.name(), node.uid());
}

void World::drainDestroyQueue() {
	std::vector<Box<Node>> doomed;
	std::swap(doomed, trees.destroy_queue);

	if (!doomed.empty()) {
		ZoneScopedN("World::drainDestroyQueue()");

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
		scrub(m_scheduler.graph.connections);
		scrub(m_scheduler.graph.inverse_connections);

		// Detach the tree structure so no victim holds a Box to another
		for (Node* victim : victims) {
			victim->m_parent = {};
			victim->m_children.clear();
			victim->m_listener.reset();
		}

		// Free the nodes
		{
			std::scoped_lock lock(nodes_mutex);
			for (Node* victim : victims) {
				ControlBox* control = ControlBox::get(victim);
				const NodeInfo* info = victim->info();
				TOAST_TRACE("World", "Destroying node {} ({})", victim->name(), victim->uid());

				if (info && info->destroy) {
					info->destroy(victim);
				} else {
					delete victim;
				}
				releaseNode(*control);
			}
		}

		computeDependencyGraph();
	}

	reapTombstones();
}

void World::markNode3DDependantsDirty(const Box<Node>& node) noexcept {
	if (!instance) {
		return;
	}

	auto it = instance->m_scheduler.graph.inverse_connections.find(node);
	if (it != instance->m_scheduler.graph.inverse_connections.end()) {
		for (auto& dependent : it->second) {
			if (auto node3d = dependent.as<Node3D>()) {
				node3d->m_dirty_world = true;
			}
		}
	}
}

void World::computeDependencyGraph() {
	ZoneScoped;

	std::vector<Box<Node>> all_nodes;
	{
		// Loader threads insert into m.nodes concurrently
		std::scoped_lock lock(nodes_mutex);
		forEachNode([&](const ControlBox& node) {
			if (!node.node || node.node->m_state == NodeState::destroy) {
				return;
			}
			all_nodes.emplace_back(node.node->box());
		});
	}

	m_scheduler.compute(all_nodes);
}

auto World::swapRoot(Node& node) -> Box<Node> {
	ZoneScoped;
	ZoneNameF("World::swapRoot() [to %s]", node.name().data());

	if (node.m_type != NodeType::root) {
		TOAST_WARN("World", "You can only swap roots with a root node");
		return {};
	}

	// The world may be empty; in that case the node is simply promoted
	auto root_node = trees.root;

	switch (node.m_state) {
		case NodeState::root: TOAST_WARN("World", "Attempted to swap root with a node already in root"); return {};
		case NodeState::destroy:
		case NodeState::loading:
		case NodeState::null:
			TOAST_WARN("World", "Attempted to swap root with a not initialized node or one that is scheduled for destruction");
			return {};
		case NodeState::cached:
			if (root_node.exists()) {
				std::ranges::replace(trees.cached, node.box(), root_node);
				root_node->m_type = NodeType::root;    // world_root only exists in root and global
				root_node->changeNodeState(NodeState::cached);
				root_node->propagateCallTick(root_node->info(), TickFunctionList::end);
				root_node->enabled(false);
			} else {
				std::erase(trees.cached, node.box());
			}
			break;
		case NodeState::global:
			if (root_node.exists()) {
				std::ranges::replace(trees.global, node.box(), root_node);
				root_node->changeNodeState(NodeState::global);
			} else {
				std::erase(trees.global, node.box());
			}
			break;
	}

	node.m_type = NodeType::world_root;
	node.changeNodeState(NodeState::root);
	trees.root = node.box();

	computeDependencyGraph();

	node.propagateCallTick(node.info(), TickFunctionList::begin);
	node.enabled(true);
	event::send<event::RequestHierarchyUpdate>();    // TODO: should the world send this?
	TOAST_INFO("World", "Swapped root to {} ({})", node.name(), node.uid());

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
				trees.root = {};
			} else {
				std::erase(node.parent()->m_children, node.box());
				node.m_parent = {};
			}
			break;
		case NodeState::global:
			if (node.m_type == NodeType::world_root) {
				std::erase(trees.global, node.box());
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

	trees.cached.emplace_back(node.box());

	computeDependencyGraph();
	TOAST_TRACE("World", "Node {} ({}) moved to cache", node.name(), node.uid());
	return node.box();
}

auto World::moveToGlobal(Node& node) -> Box<Node> {
	switch (node.m_state) {
		case NodeState::root: TOAST_WARN("World", "Tried to move root to cached, consider using swapRoot() instead"); return {};
		case NodeState::global: TOAST_WARN("World", "Tried to move to global a Node that is already in global"); return {};
		case NodeState::destroy:
		case NodeState::null:
		case NodeState::loading:
			TOAST_WARN("World", "Tried to move to cache a node that is not initialized or scheduled for destruction");
			return {};
		case NodeState::cached: break;
	}

	if (node.m_type == NodeType::child) {
		TOAST_WARN("World", "Only Root nodes can be moved to global, {} is a children node", node.name());
		return {};
	}

	std::erase(trees.cached, node.box());

	node.m_type = NodeType::world_root;
	node.changeNodeState(NodeState::global);

	computeDependencyGraph();

	node.propagateCallTick(node.info(), TickFunctionList::begin);
	node.enabled(true);
	trees.global.emplace_back(node.box());
	TOAST_TRACE("World", "Node {} ({}) moved to global", node.name(), node.uid());
	return node.box();
}

auto World::moveToChild(Node& node, Node& parent) -> Box<Node> {
	if (parent.m_state != NodeState::root) {
		TOAST_WARN("World", "You can only move a node into one that is on the root");
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
				TOAST_WARN("World", "Tried to move root to cached, consider using swapRoot() instead");
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
			std::erase(trees.global, node.box());
		} else if (node.m_state == NodeState::cached) {
			std::erase(trees.cached, node.box());
			run_begin = true;
		}
	}

	node.changeNodeState(parent.m_state);
	parent.m_children.emplace_back(node.box());
	node.m_parent = parent;

	computeDependencyGraph();

	if (run_begin) {
		node.propagateCallTick(node.info(), TickFunctionList::begin);
		node.enabled(true);
	}
	TOAST_TRACE("World", "Moved node {} ({}) under {} ({})", node.name(), node.uid(), parent.name(), parent.uid());

	return node.box();
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

			if (auto it = m_scheduler.graph.connections.find(node); it != m_scheduler.graph.connections.end()) {
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
					if (auto it = m_scheduler.graph.connections.find(curr); it != m_scheduler.graph.connections.end()) {
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

	emit_stage("early_tick", m_scheduler.schedule.early_tick);
	emit_stage("tick", m_scheduler.schedule.tick);
	emit_stage("post_physics", m_scheduler.schedule.post_physics);
	emit_stage("late_tick", m_scheduler.schedule.late_tick);

	out << "}\n";
	return out.str();
}

#pragma region TESTS

namespace _detail {

namespace {
auto testNodeInfos() -> std::unordered_map<const Node*, NodeInfo>& {
	static std::unordered_map<const Node*, NodeInfo> infos;
	return infos;
}
}

void WorldTestAccess::WorldDeleter::operator()(World* world) const noexcept {
	delete world;
}

auto WorldTestAccess::createWorld() -> WorldPtr {
	testNodeInfos().clear();
	return WorldPtr(new World());
}

auto WorldTestAccess::createNode(World& world, std::string_view name, NodeState state) -> Box<Node> {
	auto node = world.nodeAllocation();
	node->m_name = name;
	node->m_state = state;
	node->m_type = NodeType::child;
	node->m_local_enabled = true;
	node->m_inherited_enabled = true;

	NodeInfo& info = testNodeInfos()[&*node];
	info.type = "test::Node";
	info.functions.list = TickFunctionList::none;
	node->m_info = &info;

	return node;
}

void WorldTestAccess::registerDependency(Node& from, Node& to) {
	World::instance->registerDependency(from, to);
}

void WorldTestAccess::addTickStage(Node& node, TickFunctionList stage) {
	// Test-only: fabricate a per-instance NodeInfo carrying the requested tick flags so the
	// scheduler (which reads m_info) treats this node as participating in `stage`. Real nodes
	// get their NodeInfo from the type registry instead.
	NodeInfo& info = testNodeInfos()[&node];
	info.type = "test::Node";
	info.functions.list = info.functions.list | stage;
	node.m_info = &info;
}

auto WorldTestAccess::tickSchedule(World& world) noexcept -> TickSchedule& {
	return world.m_scheduler.schedule;
}

auto WorldTestAccess::dependencyGraph(World& world) noexcept -> World::DependencyGraph& {
	return world.m_scheduler.graph;
}

void WorldTestAccess::computeDependencyGraph(World& world) {
	world.computeDependencyGraph();
}

auto WorldTestAccess::instantiate(
    World& world, const assets::AssetHandle<assets::Prefab>& file, INodeOwner::InstantiateContext& ctx
) -> Box<Node> {
	return world.instantiate(file, ctx);
}

auto WorldTestAccess::childrenOf(const Node& node) -> const std::vector<Box<Node>>& {
	return node.m_children;
}

auto WorldTestAccess::isPrefabInterior(const Node& node) -> bool {
	return node.m_prefab_interior;
}

void WorldTestAccess::initThreadPool() {
	static std::unique_ptr<ThreadPool> pool = ThreadPool::create();
	(void)pool;
}

void WorldTestAccess::setWorldRoot(World& world, Node& node) {
	world.trees.root = node.box();
}

void WorldTestAccess::initAssetManager(std::string_view assets_dir, std::string_view cache_dir) {
	static std::unique_ptr<assets::AssetManager> manager;

	std::filesystem::path assets_p {std::string(assets_dir)};
	std::filesystem::path cache_path {std::string(cache_dir)};
	assets::AssetManager::setPaths(
	    assets::Paths {
	      .project = assets_p,
	      .artworks = assets_p,
	      .cache = cache_path,
	      .saved = assets_p,
	      .core = assets_p,
	    }
	);
	assets::AssetManager::registerDatabase("assets", assets_p);

	if (not manager) {
		manager = std::make_unique<assets::AssetManager>();    // ctor reads the manifest with the paths above
	} else {
		manager->reloadManifest();
	}
}

void WorldTestAccess::waitForLoads(World& world) {
	for (auto& future : world.m.load_futures) {
		if (future.valid()) {
			future.wait();
		}
	}
}

void WorldTestAccess::drainLoadQueue(World& world) {
	world.drainLoadQueue();
}

auto WorldTestAccess::spawnSync(
    World& world, const assets::AssetHandle<assets::Prefab>& file, Node& parent, INodeOwner::InstantiateContext& ctx
) -> Box<Node> {
	Box<Node> root = world.instantiate(file, ctx);
	if (not root.exists()) {
		return {};
	}
	World::generateUid(*root);
	root->changeNodeState(NodeState::cached);
	while (World::findNode(root->uid(), &parent).exists()) {
		World::generateUid(*root);
	}
	world.moveToChild(*root, parent);
	return root;
}

auto WorldTestAccess::dependencyGraphGraphviz(const World& world) -> std::string {
	return world.dependencyGraphGraphviz();
}

void WorldTestAccess::loadNode(toast::UID uid) {
	World::loadNode(uid);
}

auto WorldTestAccess::findCached(std::string_view name) -> Box<Node> {
	return World::findCached(name);
}

auto WorldTestAccess::findNode(const UID& uid, Node* scope) -> Box<Node> {
	return World::findNode(uid, scope);
}

auto WorldTestAccess::findNode(std::string_view path) -> Box<Node> {
	return World::findNode(path);
}

auto WorldTestAccess::uidPath(const Node& node) -> std::string {
	return World::uidPath(node);
}

}

#pragma endregion TESTS

}
